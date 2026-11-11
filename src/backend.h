#pragma once

#include <QByteArray>
#include <QDate>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>

// The month's money: the funds put in, the expenses taken out, and what that
// leaves for each of the remaining days. Amounts are whole cents, so sums
// never pick up floating point error.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString monthTitle READ monthTitle NOTIFY changed)
    Q_PROPERTY(int dayOfMonth READ dayOfMonth NOTIFY changed)
    Q_PROPERTY(int daysInMonth READ daysInMonth NOTIFY changed)
    Q_PROPERTY(int daysLeft READ daysLeft NOTIFY changed)
    Q_PROPERTY(qint64 fund READ fund NOTIFY changed)
    Q_PROPERTY(qint64 spent READ spent NOTIFY changed)
    Q_PROPERTY(qint64 remaining READ remaining NOTIFY changed)
    Q_PROPERTY(qint64 spentToday READ spentToday NOTIFY changed)
    Q_PROPERTY(qint64 dailyAllowance READ dailyAllowance NOTIFY changed)
    Q_PROPERTY(qint64 leftToday READ leftToday NOTIFY changed)
    Q_PROPERTY(qint64 tomorrowAllowance READ tomorrowAllowance NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    // An empty path means ~/.local/share/oma_spend/oma_spend.json, or $OMA_SPEND_DATA_FILE
    // when it is set (the screenshot harness uses that for made-up data).
    explicit Backend(const QString &path = {}, QObject *parent = nullptr);

    static QString defaultPath();
    static qint64 parseAmount(const QString &text, bool *ok);

    QString path() const { return m_path; }
    QDate today() const { return m_today; }
    // Tests pin the date; the app follows the clock.
    void setToday(const QDate &date);

    QString monthTitle() const;
    int dayOfMonth() const { return m_today.day(); }
    int daysInMonth() const { return m_today.daysInMonth(); }
    int daysLeft() const { return daysInMonth() - dayOfMonth() + 1; }
    qint64 fund() const;
    qint64 spent() const;
    qint64 remaining() const { return fund() - spent(); }
    qint64 spentToday() const;
    qint64 dailyAllowance() const;
    qint64 leftToday() const { return dailyAllowance() - spentToday(); }
    qint64 tomorrowAllowance() const;
    QVariantList entries() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString error() const { return m_error; }

    Q_INVOKABLE bool addExpense(const QString &amount, const QString &note);
    Q_INVOKABLE bool addFund(const QString &amount, const QString &note);
    Q_INVOKABLE void removeEntry(qint64 id);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE QString money(qint64 cents) const;

    QJsonObject status() const;

signals:
    void changed();

private:
    bool add(const QString &kind, const QString &amount, const QString &note);
    QString monthKey() const { return m_today.toString("yyyy-MM"); }
    QJsonArray monthEntries() const;
    void setMonthEntries(const QJsonArray &entries);
    void commit(const QJsonObject &before);
    void load();
    bool save();
    void onDiskChanged();
    void watch();
    qint64 sum(const QString &kind, const std::function<bool(const QDate &)> &when) const;

    QString m_path;
    QJsonObject m_root;
    QByteArray m_lastBytes;
    QList<QJsonObject> m_undo, m_redo;
    QString m_error;
    bool m_readOnly = false;
    bool m_pinnedDate = false;
    QDate m_today;
    QTimer m_clock;
    QFileSystemWatcher m_watcher;
};
