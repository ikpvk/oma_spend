// oma_spend — this month's fund, what's spent, and what's left to spend each day.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "theme.h"
#include "translator.h"

namespace {
const char *usage =
    "Usage:\n"
    "  oma_spend [open]                 open the window\n"
    "  oma_spend status [--json]        this month's fund, spending and daily allowance\n"
    "  oma_spend spend <amount> [note]  record an expense for today\n"
    "  oma_spend fund <amount> [note]   add money to this month's fund\n";

void printStatus(const Backend &backend, bool json) {
    QTextStream out(stdout);
    const QJsonObject status = backend.status();
    if (json) {
        out << QJsonDocument(status).toJson(QJsonDocument::Compact) << "\n";
        return;
    }
    out << backend.monthTitle() << ", day " << backend.dayOfMonth() << " of " << backend.daysInMonth()
        << " (" << backend.daysLeft() << " days left)\n"
        << "Fund:       " << backend.money(backend.fund()) << "\n"
        << "Spent:      " << backend.money(backend.spent()) << "\n"
        << "Left:       " << backend.money(backend.remaining()) << "\n"
        << "Per day:    " << backend.money(backend.dailyAllowance()) << "\n"
        << "Left today: " << backend.money(backend.leftToday()) << "\n";
}

// Commands that need no display, so a terminal or an agent can record and
// check spending. Exits 0 on success and 1 with the reason on stderr.
int runCommand(const QStringList &args) {
    QTextStream err(stderr);
    const QString command = args.value(0);
    const bool json = args.contains("--json");
    QStringList rest = args.mid(1);
    rest.removeAll("--json");

    Backend backend;
    if (!backend.error().isEmpty()) {
        err << backend.error() << "\n";
        return 1;
    }
    if (command == "status") {
        printStatus(backend, json);
        return 0;
    }
    if (command == "spend" || command == "fund") {
        if (rest.isEmpty()) {
            err << usage;
            return 1;
        }
        const QString amount = rest.takeFirst();
        const QString note = rest.join(' ');
        const bool ok = command == "spend" ? backend.addExpense(amount, note) : backend.addFund(amount, note);
        if (!ok) {
            err << backend.error() << "\n";
            return 1;
        }
        printStatus(backend, json);
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        QTextStream(stdout) << usage;
        return 0;
    }
    err << usage;
    return 1;
}
}

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    if (!args.isEmpty() && args.first() != "open") {
        QCoreApplication app(argc, argv);
        app.setApplicationName("oma_spend");
        return runCommand(args);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("oma_spend");
    app.setApplicationVersion("0.1.0");

    // Matches the window to oma_spend.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("oma_spend");
    app.setWindowIcon(QIcon::fromTheme("oma_spend"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    // Month names and money follow the language's locale, so re-render them.
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::changed);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
