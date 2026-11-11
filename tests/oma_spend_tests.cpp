#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "theme.h"
#include "translator.h"

class OmaSpendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Money and number parsing are checked against one fixed locale.
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }

    void parsesAmounts()
    {
        bool ok = false;
        QCOMPARE(Backend::parseAmount("12.5", &ok), 1250);
        QVERIFY(ok);
        QCOMPARE(Backend::parseAmount(" $1,250.05 ", &ok), 125005);
        QVERIFY(ok);
        QCOMPARE(Backend::parseAmount("0.1", &ok), 10);
        Backend::parseAmount("0", &ok);
        QVERIFY(!ok);
        Backend::parseAmount("-5", &ok);
        QVERIFY(!ok);
        Backend::parseAmount("lunch", &ok);
        QVERIFY(!ok);
    }

    void splitsTheFundOverTheDaysLeft()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        backend.setToday(QDate(2026, 11, 11));
        QCOMPARE(backend.daysInMonth(), 30);
        QCOMPARE(backend.daysLeft(), 20);
        QCOMPARE(backend.dailyAllowance(), 0);

        QVERIFY(backend.addFund("2000", "Salary"));
        QVERIFY(backend.addFund("1000", "Top-up"));
        QCOMPARE(backend.fund(), 300000);
        QCOMPARE(backend.dailyAllowance(), 15000);
        QCOMPARE(backend.leftToday(), 15000);
    }

    void earlierSpendingLowersTheAllowanceAndTodaysLowersWhatsLeftToday()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        backend.setToday(QDate(2026, 11, 5));
        QVERIFY(backend.addFund("3000", ""));
        QVERIFY(backend.addExpense("40", "Groceries"));

        backend.setToday(QDate(2026, 11, 11));
        QCOMPARE(backend.dailyAllowance(), 14800); // (3000 - 40) / 20
        QVERIFY(backend.addExpense("20", "Lunch"));
        QCOMPARE(backend.dailyAllowance(), 14800);
        QCOMPARE(backend.spentToday(), 2000);
        QCOMPARE(backend.leftToday(), 12800);
        QCOMPARE(backend.tomorrowAllowance(), 15473); // 2940 / 19, rounded down
        QCOMPARE(backend.remaining(), 294000);
    }

    void lastDayHasNoTomorrow()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        backend.setToday(QDate(2026, 11, 30));
        QVERIFY(backend.addFund("90", ""));
        QCOMPARE(backend.daysLeft(), 1);
        QCOMPARE(backend.dailyAllowance(), 9000);
        QCOMPARE(backend.tomorrowAllowance(), 0);
    }

    void eachMonthStartsFresh()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        backend.setToday(QDate(2026, 11, 20));
        QVERIFY(backend.addFund("500", ""));
        backend.setToday(QDate(2026, 12, 1));
        QCOMPARE(backend.fund(), 0);
        QCOMPARE(backend.entries().size(), 0);
        QCOMPARE(backend.monthTitle(), QString("December 2026"));
    }

    void savesAndLoads()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("nested/oma_spend.json");
        {
            Backend backend(path);
            backend.setToday(QDate(2026, 11, 11));
            QVERIFY(backend.addFund("100", ""));
            QVERIFY(backend.addExpense("12.34", "Coffee"));
        }
        Backend backend(path);
        backend.setToday(QDate(2026, 11, 11));
        QCOMPARE(backend.spent(), 1234);
        const QVariantMap newest = backend.entries().first().toMap();
        QCOMPARE(newest.value("note").toString(), QString("Coffee"));
        QCOMPARE(newest.value("kind").toString(), QString("expense"));
    }

    void removesAndUndoes()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        backend.setToday(QDate(2026, 11, 11));
        QVERIFY(backend.addFund("100", ""));
        QVERIFY(backend.addExpense("10", ""));
        const qint64 id = backend.entries().first().toMap().value("id").toLongLong();
        backend.removeEntry(id);
        QCOMPARE(backend.spent(), 0);
        backend.undo();
        QCOMPARE(backend.spent(), 1000);
        backend.redo();
        QCOMPARE(backend.spent(), 0);
    }

    void rejectsBadAmountsWithAReason()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        QVERIFY(!backend.addExpense("abc", ""));
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!QFile::exists(dir.filePath("oma_spend.json")));
    }

    void neverOverwritesUnreadableData()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        write(path, "not json");
        Backend backend(path);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!backend.addExpense("5", ""));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("not json"));
    }

    void picksUpChangesFromTheCommandLine()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        Backend window(path);
        window.setToday(QDate::currentDate());
        QVERIFY(window.addFund("100", ""));

        Backend command(path);
        QVERIFY(command.addExpense("7", "Snack"));
        QTRY_COMPARE(window.spent(), 700);
    }

    void formatsDollars()
    {
        Backend backend(QTemporaryDir().filePath("unused.json"));
        QCOMPARE(backend.money(125005), QString("$1,250.05"));
        QCOMPARE(backend.money(-999), QString("−$9.99"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorPicksEnglishPlurals()
    {
        Translator translator(QTemporaryDir().filePath("language"), ":/i18n");
        QCOMPARE(Translator::pluralCategory("en", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("en", 20), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(OmaSpendTests)
#include "oma_spend_tests.moc"
