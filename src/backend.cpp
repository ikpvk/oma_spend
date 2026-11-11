#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {
// Whole cents divided by days, rounded down so following the allowance never
// overspends by a fraction of a cent.
qint64 perDay(qint64 cents, int days) {
    if (days <= 0)
        return 0;
    const qint64 quotient = cents / days; // truncates toward zero
    return (cents % days != 0 && cents < 0) ? quotient - 1 : quotient;
}

// A JSON number that holds a whole value from `min` to Backend::maxCents.
// JSON numbers are doubles, which skip integers beyond that.
bool isWhole(const QJsonValue &value, qint64 min) {
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return std::isfinite(number) && number == std::floor(number) && number >= static_cast<double>(min)
        && number <= static_cast<double>(Backend::maxCents);
}

// Reads "1,250.05", in the locale's separators, straight into cents with no
// floating point, so the limit is exact. Digits past the cents round half up.
// Groups must be three digits, so "1250.50" isn't misread where "." groups.
bool readCents(const QString &text, const QLocale &locale, qint64 *cents) {
    const QString point = locale.decimalPoint();
    const QString group = locale.groupSeparator();
    QString whole = text, fraction;
    const qsizetype at = text.indexOf(point);
    if (at >= 0) {
        whole = text.left(at);
        fraction = text.mid(at + point.size());
    }
    if (!group.isEmpty() && whole.contains(group)) {
        const QStringList parts = whole.split(group);
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (parts.at(i).isEmpty() || parts.at(i).size() > 3 || (i > 0 && parts.at(i).size() != 3))
                return false;
        }
        whole = parts.join(QString());
    }
    const auto digitsOnly = [](const QString &s) {
        return std::all_of(s.begin(), s.end(), [](QChar c) { return c >= '0' && c <= '9'; });
    };
    if ((whole.isEmpty() && fraction.isEmpty()) || !digitsOnly(whole) || !digitsOnly(fraction))
        return false;

    while (whole.size() > 1 && whole.startsWith('0'))
        whole.remove(0, 1);
    if (whole.size() > 15) // far above the limit; stops qint64 overflowing below
        *cents = Backend::maxCents + 1;
    else {
        const QString hundredths = (fraction + "00").left(2);
        *cents = (whole.isEmpty() ? 0 : whole.toLongLong()) * 100 + hundredths.toLongLong()
            + (fraction.size() > 2 && fraction.at(2) >= '5' ? 1 : 0);
    }
    return true;
}

// Checks the whole file before it's trusted, so a file from a newer version
// or a hand edit gone wrong is refused rather than rewritten. Returns why the
// data is invalid, or an empty string when it's fine.
QString validate(const QJsonObject &root) {
    if (root.value("version") != QJsonValue(1))
        return Backend::tr("unsupported version %1").arg(root.value("version").toVariant().toString());
    if (root.contains("lastId") && !isWhole(root.value("lastId"), 0))
        return Backend::tr("lastId isn't a whole number");
    if (!root.value("months").isObject())
        return Backend::tr("months isn't an object");

    static const QRegularExpression monthPattern("^\\d{4}-\\d{2}$");
    static const QRegularExpression datePattern("^\\d{4}-\\d{2}-\\d{2}$");
    const QJsonObject months = root.value("months").toObject();
    for (auto it = months.begin(); it != months.end(); ++it) {
        const QString key = it.key();
        if (!monthPattern.match(key).hasMatch() || !QDate::fromString(key + "-01", Qt::ISODate).isValid())
            return Backend::tr("%1 isn't a YYYY-MM month").arg(key);
        if (!it.value().isObject() || !it.value().toObject().value("entries").isArray())
            return Backend::tr("month %1 has no entries list").arg(key);
        const QJsonArray entries = it.value().toObject().value("entries").toArray();
        qint64 funds = 0, expenses = 0;
        for (qsizetype i = 0; i < entries.size(); ++i) {
            const QJsonObject entry = entries.at(i).toObject();
            const QString kind = entry.value("kind").toString();
            const QString date = entry.value("date").toString();
            const bool ok = entries.at(i).isObject()
                && isWhole(entry.value("id"), 1)
                && (kind == "fund" || kind == "expense")
                && isWhole(entry.value("cents"), 1)
                && datePattern.match(date).hasMatch()
                && QDate::fromString(date, Qt::ISODate).isValid()
                && date.startsWith(key + "-")
                && (!entry.contains("note") || entry.value("note").isString());
            if (!ok)
                return Backend::tr("entry %1 of %2 is malformed").arg(i + 1).arg(key);
            qint64 &total = kind == "fund" ? funds : expenses;
            total += entry.value("cents").toInteger(); // each is at most maxCents, so no overflow
            if (total > Backend::maxCents)
                return Backend::tr("%1's total is larger than oma_spend can track").arg(key);
        }
    }
    return {};
}
}

Backend::Backend(const QString &path, QObject *parent)
    : QObject(parent), m_path(path.isEmpty() ? defaultPath() : path), m_today(QDate::currentDate()) {
    load();

    // The allowance depends on the date, so check the clock and move on to
    // the next day, or the next month, while the window stays open.
    m_clock.setInterval(30 * 1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        if (!m_pinnedDate && QDate::currentDate() != m_today) {
            m_today = QDate::currentDate();
            emit changed();
        }
    });
    m_clock.start();

    // `oma_spend spend` from a terminal or an agent writes the same file, so pick
    // up changes made while the window is open. Saving replaces the file, so
    // watch the directory too and re-arm after every change.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Backend::onDiskChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Backend::onDiskChanged);
    watch();
}

QString Backend::defaultPath() {
    const QString overridden = qEnvironmentVariable("OMA_SPEND_DATA_FILE");
    if (!overridden.isEmpty())
        return overridden;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/oma_spend/oma_spend.json";
}

// Accepts the user's own number format ("1,250.50") and a plain one
// ("1250.50"), with or without a leading dollar sign.
qint64 Backend::parseAmount(const QString &text, bool *ok, bool *tooLarge) {
    QString cleaned = text.trimmed();
    cleaned.remove('$');
    cleaned = cleaned.trimmed();
    qint64 cents = 0;
    const bool parsed = readCents(cleaned, QLocale(), &cents) || readCents(cleaned, QLocale::c(), &cents);
    const bool valid = parsed && cents > 0 && cents <= maxCents;
    if (ok)
        *ok = valid;
    if (tooLarge)
        *tooLarge = parsed && cents > maxCents;
    return valid ? cents : 0;
}

void Backend::setToday(const QDate &date) {
    m_pinnedDate = true;
    m_today = date;
    emit changed();
}

QString Backend::monthTitle() const {
    const QLocale locale;
    return locale.standaloneMonthName(m_today.month()) + " " + QString::number(m_today.year());
}

QJsonArray Backend::monthEntries() const {
    return m_root.value("months").toObject().value(monthKey()).toObject().value("entries").toArray();
}

QJsonObject Backend::withMonthEntries(QJsonObject root, const QJsonArray &entries) const {
    QJsonObject months = root.value("months").toObject();
    QJsonObject month = months.value(monthKey()).toObject();
    month.insert("entries", entries);
    months.insert(monthKey(), month);
    root.insert("months", months);
    return root;
}

qint64 Backend::sum(const QString &kind, const std::function<bool(const QDate &)> &when) const {
    qint64 total = 0;
    for (const auto &value : monthEntries()) {
        const QJsonObject entry = value.toObject();
        if (entry.value("kind").toString() != kind)
            continue;
        if (when(QDate::fromString(entry.value("date").toString(), Qt::ISODate)))
            total += entry.value("cents").toInteger();
    }
    return total;
}

qint64 Backend::fund() const {
    return sum("fund", [](const QDate &) { return true; });
}

qint64 Backend::spent() const {
    return sum("expense", [](const QDate &) { return true; });
}

qint64 Backend::spentToday() const {
    return sum("expense", [this](const QDate &d) { return d == m_today; });
}

// What's left at the start of today, shared evenly over today and the rest of
// the month. Spending today doesn't change today's allowance; it changes
// what's left today, and tomorrow's allowance.
qint64 Backend::dailyAllowance() const {
    const qint64 spentBefore = sum("expense", [this](const QDate &d) { return d < m_today; });
    return perDay(fund() - spentBefore, daysLeft());
}

qint64 Backend::tomorrowAllowance() const {
    return perDay(remaining(), daysLeft() - 1);
}

QVariantList Backend::entries() const {
    QList<QJsonObject> list;
    for (const auto &value : monthEntries())
        list.append(value.toObject());
    std::sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString da = a.value("date").toString(), db = b.value("date").toString();
        if (da != db)
            return da > db;
        return a.value("id").toInteger() > b.value("id").toInteger();
    });

    const QLocale locale;
    QVariantList result;
    for (const auto &entry : list) {
        const QDate date = QDate::fromString(entry.value("date").toString(), Qt::ISODate);
        result.append(QVariantMap{
            {"id", entry.value("id").toInteger()},
            {"kind", entry.value("kind").toString()},
            {"cents", entry.value("cents").toInteger()},
            {"note", entry.value("note").toString()},
            {"day", locale.toString(date, "d MMM")},
            {"today", date == m_today},
        });
    }
    return result;
}

// A plain minus sign rather than the accounting style "($9.99)" that some
// locales use for negative amounts.
QString Backend::money(qint64 cents) const {
    const QString amount = QLocale().toCurrencyString(std::abs(static_cast<double>(cents)) / 100.0, "$", 2);
    return cents < 0 ? QStringLiteral("−") + amount : amount;
}

bool Backend::addExpense(const QString &amount, const QString &note) {
    return add("expense", amount, note);
}

bool Backend::addFund(const QString &amount, const QString &note) {
    return add("fund", amount, note);
}

bool Backend::add(const QString &kind, const QString &amount, const QString &note) {
    bool ok = false, tooLarge = false;
    const qint64 cents = parseAmount(amount, &ok, &tooLarge);
    if (!ok) {
        m_error = tooLarge ? tr("That's more than oma_spend can track. Enter a smaller amount.")
                           : tr("Enter an amount above zero, like 12.50.");
        emit changed();
        return false;
    }
    QLockFile lock(m_path + ".lock");
    if (!beginWrite(lock)) {
        emit changed();
        return false;
    }

    // Totals stay within the same limit, so sums never overflow and the file
    // always passes validation.
    if (sum(kind, [](const QDate &) { return true; }) > maxCents - cents) {
        m_error = tr("That would take this month's total past what oma_spend can track.");
        emit changed();
        return false;
    }

    // Ids only need to be unique and to grow, so the newest entry sorts first
    // within a day.
    const qint64 id = std::max(QDateTime::currentMSecsSinceEpoch(), m_root.value("lastId").toInteger() + 1);
    QJsonArray list = monthEntries();
    list.append(QJsonObject{
        {"id", id},
        {"kind", kind},
        {"cents", cents},
        {"date", m_today.toString(Qt::ISODate)},
        {"note", note.trimmed()},
    });
    QJsonObject after = withMonthEntries(m_root, list);
    after.insert("lastId", id);
    commit(m_root, after);
    return m_error.isEmpty();
}

void Backend::removeEntry(qint64 id) {
    QLockFile lock(m_path + ".lock");
    if (!beginWrite(lock)) {
        emit changed();
        return;
    }
    QJsonArray list = monthEntries();
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).toObject().value("id").toInteger() == id) {
            list.removeAt(i);
            commit(m_root, withMonthEntries(m_root, list));
            return;
        }
    }
    emit changed();
}

// Adopts `after` only once it's on disk, so a failed save leaves the records
// and the undo history exactly as they were, and retrying can't double up.
void Backend::commit(QJsonObject before, const QJsonObject &after) {
    if (save(after)) {
        m_undo.append(before);
        if (m_undo.size() > 100)
            m_undo.removeFirst();
        m_redo.clear();
    }
    emit changed();
}

void Backend::undo() {
    QLockFile lock(m_path + ".lock");
    bool historyReset = false;
    if (m_undo.isEmpty() || !beginWrite(lock, &historyReset) || historyReset) {
        emit changed();
        return;
    }
    const QJsonObject current = m_root;
    if (save(m_undo.last())) {
        m_undo.removeLast();
        m_redo.append(current);
    }
    emit changed();
}

void Backend::redo() {
    QLockFile lock(m_path + ".lock");
    bool historyReset = false;
    if (m_redo.isEmpty() || !beginWrite(lock, &historyReset) || historyReset) {
        emit changed();
        return;
    }
    const QJsonObject current = m_root;
    if (save(m_redo.last())) {
        m_redo.removeLast();
        m_undo.append(current);
    }
    emit changed();
}

// Every change goes through here: it takes the lock that other oma_spend
// processes honour, then catches up with anything they saved since we last
// read, so a change is always made on top of the latest records. When the
// records did change, our undo history no longer matches them and starts over.
bool Backend::beginWrite(QLockFile &lock, bool *historyReset) {
    if (historyReset)
        *historyReset = false;
    if (m_readOnly)
        return false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!lock.tryLock(5000)) {
        if (lock.error() == QLockFile::LockFailedError)
            m_error = tr("Couldn't lock %1. Another oma_spend may be saving; try again.").arg(m_path);
        else if (lock.error() == QLockFile::PermissionError)
            m_error = tr("Couldn't save to %1: no permission to write its folder.").arg(m_path);
        else
            m_error = tr("Couldn't save to %1.").arg(m_path);
        return false;
    }

    QByteArray bytes;
    QFile file(m_path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            m_error = tr("Can't read %1: %2").arg(m_path, file.errorString());
            return false;
        }
        bytes = file.readAll();
    }
    if (bytes != m_lastBytes) {
        load();
        m_undo.clear();
        m_redo.clear();
        if (historyReset)
            *historyReset = true;
        if (m_readOnly)
            return false;
    }
    m_error.clear();
    return true;
}

void Backend::load() {
    QFile file(m_path);
    if (!file.exists()) {
        m_root = QJsonObject{{"version", 1}, {"months", QJsonObject{}}};
        m_lastBytes.clear();
        m_readOnly = false;
        m_error.clear();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = tr("Can't read %1: %2").arg(m_path, file.errorString());
        m_readOnly = true;
        return;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    // Never overwrite a file we can't understand; it may be the only copy of
    // the user's records.
    QString problem = document.isObject() ? validate(document.object()) : parseError.errorString();
    if (!document.isObject() && parseError.error == QJsonParseError::NoError)
        problem = tr("not a JSON object");
    if (!problem.isEmpty()) {
        m_error = tr("%1 isn't valid oma_spend data (%2). Fix or move it, then restart.").arg(m_path, problem);
        m_readOnly = true;
        return;
    }
    m_root = document.object();
    m_lastBytes = bytes;
    m_readOnly = false;
    m_error.clear();
}

bool Backend::save(const QJsonObject &root) {
    if (m_readOnly)
        return false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    // QSaveFile writes beside the target and renames it into place, so a
    // crash mid-write never leaves a half-written file.
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = tr("Couldn't save to %1: %2").arg(m_path, file.errorString());
        return false;
    }
    m_root = root;
    m_lastBytes = bytes;
    m_error.clear();
    watch();
    return true;
}

void Backend::watch() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    const QString directory = QFileInfo(m_path).absolutePath();
    if (QFileInfo::exists(directory))
        m_watcher.addPath(directory);
    if (QFileInfo::exists(m_path))
        m_watcher.addPath(m_path);
}

void Backend::onDiskChanged() {
    watch();
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    if (bytes == m_lastBytes)
        return;
    // Someone else changed the records; our undo history would overwrite
    // their change, so it starts over from what's on disk.
    load();
    m_undo.clear();
    m_redo.clear();
    emit changed();
}

QJsonObject Backend::status() const {
    return QJsonObject{
        {"month", monthKey()},
        {"day", dayOfMonth()},
        {"days_in_month", daysInMonth()},
        {"days_left", daysLeft()},
        {"fund_cents", fund()},
        {"spent_cents", spent()},
        {"remaining_cents", remaining()},
        {"spent_today_cents", spentToday()},
        {"daily_allowance_cents", dailyAllowance()},
        {"left_today_cents", leftToday()},
        {"tomorrow_allowance_cents", tomorrowAllowance()},
    };
}
