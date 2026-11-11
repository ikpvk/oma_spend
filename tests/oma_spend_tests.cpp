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
        Backend::parseAmount("-0.5", &ok);
        QVERIFY(!ok);
        Backend::parseAmount("1e3", &ok);
        QVERIFY(!ok);
        QCOMPARE(Backend::parseAmount("1250", &ok), 125000);
        QCOMPARE(Backend::parseAmount(".5", &ok), 50);
        QCOMPARE(Backend::parseAmount("0.125", &ok), 13);
        Backend::parseAmount("12,50", &ok);
        QVERIFY(!ok);
    }

    void amountsStopAtTheLargestStorableCents()
    {
        bool ok = false, tooLarge = false;
        QCOMPARE(Backend::maxCents, Q_INT64_C(9007199254740991));
        QCOMPARE(Backend::parseAmount("90071992547409.91", &ok, &tooLarge), Backend::maxCents);
        QVERIFY(ok && !tooLarge);
        QCOMPARE(Backend::parseAmount("$90,071,992,547,409.91", &ok), Backend::maxCents);
        QVERIFY(ok);
        QCOMPARE(Backend::parseAmount("90071992547409.92", &ok, &tooLarge), 0);
        QVERIFY(!ok && tooLarge);
        Backend::parseAmount("1000000000000000000000000", &ok, &tooLarge);
        QVERIFY(!ok && tooLarge);

        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        Backend backend(path);
        backend.setToday(QDate(2026, 11, 11));
        QVERIFY(!backend.addFund("90071992547409.92", ""));
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!QFile::exists(path));

        // The largest amount saves and loads back; one more cent in the
        // month's total is refused, so the file stays valid.
        QVERIFY(backend.addFund("90071992547409.91", ""));
        QVERIFY(!backend.addFund("0.01", ""));
        QVERIFY(backend.addExpense("90071992547409.91", ""));
        Backend reader(path);
        reader.setToday(QDate(2026, 11, 11));
        QVERIFY(reader.error().isEmpty());
        QCOMPARE(reader.fund(), Backend::maxCents);
        QCOMPARE(reader.spent(), Backend::maxCents);
    }

    void allowanceIsExactAtTheLimit()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("oma_spend.json"));
        backend.setToday(QDate(2026, 11, 30));
        QVERIFY(backend.addFund("90071992547409.91", ""));
        QCOMPARE(backend.dailyAllowance(), Backend::maxCents);
        backend.setToday(QDate(2026, 11, 29));
        QCOMPARE(backend.dailyAllowance(), Backend::maxCents / 2); // rounded down
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

    void twoInstancesKeepEachOthersRecords()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        Backend window(path);
        Backend command(path);
        window.setToday(QDate(2026, 11, 11));
        command.setToday(QDate(2026, 11, 11));

        // Neither has seen the other's save; no event loop runs in between.
        QVERIFY(window.addFund("100", ""));
        QVERIFY(command.addExpense("7", ""));

        Backend reader(path);
        reader.setToday(QDate(2026, 11, 11));
        QCOMPARE(reader.fund(), 10000);
        QCOMPARE(reader.spent(), 700);
    }

    void undoNeverRevertsAnotherInstancesChange()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        Backend window(path);
        window.setToday(QDate(2026, 11, 11));
        QVERIFY(window.addFund("100", ""));
        QVERIFY(window.addExpense("5", ""));

        Backend command(path);
        command.setToday(QDate(2026, 11, 11));
        QVERIFY(command.addExpense("7", ""));

        window.undo();
        QVERIFY(!window.canUndo());
        Backend reader(path);
        reader.setToday(QDate(2026, 11, 11));
        QCOMPARE(reader.spent(), 1200);
    }

    void failedSaveLeavesNoTrace()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath("data");
        const QString path = folder + "/oma_spend.json";
        Backend backend(path);
        backend.setToday(QDate(2026, 11, 11));
        QVERIFY(backend.addFund("100", ""));

        const auto writable = QFile::permissions(folder);
        QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFile(folder + "/probe").open(QIODevice::WriteOnly)) {
            QFile::setPermissions(folder, writable);
            QSKIP("Running with permissions that ignore a read-only folder");
        }
        QVERIFY(!backend.addExpense("7", ""));
        QVERIFY(!backend.error().isEmpty());
        QCOMPARE(backend.spent(), 0);
        QFile::setPermissions(folder, writable);

        // Retrying after the folder is writable again records it once.
        QVERIFY(backend.addExpense("7", ""));
        QCOMPARE(backend.spent(), 700);
        Backend reader(path);
        reader.setToday(QDate(2026, 11, 11));
        QCOMPARE(reader.spent(), 700);
        backend.undo();
        QCOMPARE(backend.spent(), 0);
        QCOMPARE(backend.fund(), 10000);
    }

    void neverOverwritesMalformedData_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("not an object") << QByteArray("[]");
        QTest::newRow("newer version") << QByteArray(R"({"version":2,"months":{}})");
        QTest::newRow("no version") << QByteArray(R"({"months":{}})");
        QTest::newRow("months is a list") << QByteArray(R"({"version":1,"months":[]})");
        QTest::newRow("bad month key") << QByteArray(R"({"version":1,"months":{"2026-13":{"entries":[]}}})");
        QTest::newRow("no entries") << QByteArray(R"({"version":1,"months":{"2026-11":{}}})");
        const QByteArray entry = R"({"id":1,"kind":"expense","cents":700,"date":"2026-11-11","note":""})";
        auto withEntry = [](QByteArray e) {
            return QByteArray(R"({"version":1,"months":{"2026-11":{"entries":[)") + e + "]}}}";
        };
        QTest::newRow("unknown kind") << withEntry(QByteArray(entry).replace("expense", "gift"));
        QTest::newRow("cents as text") << withEntry(QByteArray(entry).replace("700", "\"700\""));
        QTest::newRow("fractional cents") << withEntry(QByteArray(entry).replace("700", "7.5"));
        QTest::newRow("zero cents") << withEntry(QByteArray(entry).replace("700", "0"));
        QTest::newRow("date in another month") << withEntry(QByteArray(entry).replace("2026-11-11", "2026-10-11"));
        QTest::newRow("no id") << withEntry(QByteArray(entry).replace(R"("id":1,)", ""));
        QTest::newRow("entry not an object") << withEntry("7");
        QTest::newRow("cents past 2^53") << withEntry(QByteArray(entry).replace("700", "9007199254740992"));
        QTest::newRow("month total past 2^53")
            << withEntry(QByteArray(entry).replace("700", "9007199254740991") + ","
                         + QByteArray(entry).replace(R"("id":1)", R"("id":2)"));
    }

    void neverOverwritesMalformedData()
    {
        QFETCH(QByteArray, contents);
        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        write(path, contents);
        Backend backend(path);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!backend.addExpense("5", ""));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), contents);
    }

    void loadsValidHandWrittenData()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("oma_spend.json");
        write(path, R"({"version":1,"months":{"2026-11":{"entries":[)"
                    R"({"id":1,"kind":"fund","cents":10000,"date":"2026-11-01","note":"Salary"}]}}})");
        Backend backend(path);
        backend.setToday(QDate(2026, 11, 11));
        QVERIFY(backend.error().isEmpty());
        QCOMPARE(backend.fund(), 10000);
        QVERIFY(backend.addExpense("5", ""));
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
