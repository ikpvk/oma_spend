#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
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
    return static_cast<qint64>(std::floor(static_cast<double>(cents) / days));
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
qint64 Backend::parseAmount(const QString &text, bool *ok) {
    QString cleaned = text.trimmed();
    cleaned.remove('$');
    cleaned = cleaned.trimmed();
    bool parsed = false;
    double value = QLocale().toDouble(cleaned, &parsed);
    if (!parsed)
        value = QLocale::c().toDouble(cleaned, &parsed);
    const qint64 cents = parsed ? std::llround(value * 100.0) : 0;
    const bool valid = parsed && std::isfinite(value) && cents > 0;
    if (ok)
        *ok = valid;
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

void Backend::setMonthEntries(const QJsonArray &entries) {
    QJsonObject months = m_root.value("months").toObject();
    QJsonObject month = months.value(monthKey()).toObject();
    month.insert("entries", entries);
    months.insert(monthKey(), month);
    m_root.insert("months", months);
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
    bool ok = false;
    const qint64 cents = parseAmount(amount, &ok);
    if (!ok) {
        m_error = tr("Enter an amount above zero, like 12.50.");
        emit changed();
        return false;
    }
    if (m_readOnly) {
        emit changed();
        return false;
    }

    const QJsonObject before = m_root;
    // Ids only need to be unique and to grow, so the newest entry sorts first
    // within a day.
    const qint64 id = std::max(QDateTime::currentMSecsSinceEpoch(), m_root.value("lastId").toInteger() + 1);
    m_root.insert("lastId", id);
    QJsonArray list = monthEntries();
    list.append(QJsonObject{
        {"id", id},
        {"kind", kind},
        {"cents", cents},
        {"date", m_today.toString(Qt::ISODate)},
        {"note", note.trimmed()},
    });
    setMonthEntries(list);
    commit(before);
    return m_error.isEmpty();
}

void Backend::removeEntry(qint64 id) {
    if (m_readOnly)
        return;
    const QJsonObject before = m_root;
    QJsonArray list = monthEntries();
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).toObject().value("id").toInteger() == id) {
            list.removeAt(i);
            setMonthEntries(list);
            commit(before);
            return;
        }
    }
}

void Backend::commit(const QJsonObject &before) {
    m_undo.append(before);
    if (m_undo.size() > 100)
        m_undo.removeFirst();
    m_redo.clear();
    m_error.clear();
    save();
    emit changed();
}

void Backend::undo() {
    if (m_undo.isEmpty() || m_readOnly)
        return;
    m_redo.append(m_root);
    m_root = m_undo.takeLast();
    m_error.clear();
    save();
    emit changed();
}

void Backend::redo() {
    if (m_redo.isEmpty() || m_readOnly)
        return;
    m_undo.append(m_root);
    m_root = m_redo.takeLast();
    m_error.clear();
    save();
    emit changed();
}

void Backend::load() {
    QFile file(m_path);
    if (!file.exists()) {
        m_root = QJsonObject{{"version", 1}, {"months", QJsonObject{}}};
        m_readOnly = false;
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
    if (!document.isObject()) {
        // Never overwrite a file we can't understand; it may be the only copy
        // of the user's records.
        m_error = tr("%1 isn't valid oma_spend data (%2). Fix or move it, then restart.")
                      .arg(m_path, parseError.errorString());
        m_readOnly = true;
        return;
    }
    m_root = document.object();
    m_lastBytes = bytes;
    m_readOnly = false;
    m_error.clear();
}

bool Backend::save() {
    if (m_readOnly)
        return false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QByteArray bytes = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
    // QSaveFile writes beside the target and renames it into place, so a
    // crash mid-write never leaves a half-written file.
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = tr("Couldn't save to %1: %2").arg(m_path, file.errorString());
        return false;
    }
    m_lastBytes = bytes;
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
