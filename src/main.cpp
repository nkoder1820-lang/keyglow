// keyglow — backlight scenes for the Cosmic Byte CB Artemis keyboard.

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "device.h"
#include "sceneimages.h"
#include "streamer.h"
#include "theme.h"
#include "translator.h"

// `keyglow daemon` keeps the lights on without a window. Only one may run:
// two streamers would fight over the keyboard.
static int runDaemon(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    QLockFile lock(runtime + "/keyglow.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return 0;
    Streamer streamer;
    return app.exec();
}

// Commands that need no window, so the lights can be bound to keys or scripts:
//   keyglow list | status | toggle | on | off | next | prev
//   keyglow list --json | status --json   (for bars and scripts)
//   keyglow white <red%> <blue%>          (white balance for this keyboard)
//   keyglow preset <id> | brightness <n|+n|-n> | speed <n|+n|-n> | color <#rrggbb>
// Returns -1 when `command` isn't one of them.
static int runCommand(const QStringList &args) {
    QTextStream out(stdout), err(stderr);
    const QString command = args.value(0), value = args.value(1);
    const QString path = Settings::defaultPath();
    Settings s = Settings::load(path);
    const auto &presets = Engine::presets();
    auto adjust = [&](int current) {
        bool ok = false;
        const int n = value.toInt(&ok);
        if (!ok)
            return -1;
        return qBound(0, value.startsWith('+') || value.startsWith('-') ? current + n : n, 100);
    };
    auto shift = [&](int by) {
        int index = 0;
        for (int i = 0; i < presets.size(); ++i)
            if (presets[i].id == s.preset)
                index = i;
        s.preset = presets[(index + by + presets.size()) % presets.size()].id;
        s.power = true;
    };

    const bool json = args.contains("--json");
    if (command == "list" && json) {
        out << QJsonDocument(Cli::scenes(Backend({}, false))).toJson(QJsonDocument::Compact) << "\n";
        return 0;
    }
    if (command == "status" && json) {
        out << QJsonDocument(Cli::status(Backend({}, false))).toJson(QJsonDocument::Compact) << "\n";
        return 0;
    }
    if (command == "list") {
        for (const Preset &p : presets)
            out << p.id << "\n";
        return 0;
    }
    if (command == "status") {
        out << (s.power ? "on" : "off") << " " << s.preset << " " << s.brightness << "% " << s.color.name() << "\n";
        return 0;
    }
    if (command == "toggle") s.power = !s.power;
    else if (command == "on") s.power = true;
    else if (command == "off") s.power = false;
    else if (command == "next") shift(1);
    else if (command == "prev") shift(-1);
    else if (command == "preset") {
        if (Engine::preset(value).id != value) {
            err << "keyglow: no preset called '" << value << "' (see: keyglow list)\n";
            return 1;
        }
        s.preset = value;
        s.power = true;
    } else if (command == "brightness" || command == "speed") {
        int &target = command == "speed" ? s.speed : s.brightness;
        const int next = adjust(target);
        if (next < 0) {
            err << "keyglow: " << command << " takes a number from 0 to 100, or +n / -n\n";
            return 1;
        }
        target = next;
    } else if (command == "white") {
        // keyglow white <red%> <blue%>: how much red and blue make white next to full green.
        bool okRed = false, okBlue = false;
        const int red = value.toInt(&okRed), blue = args.value(2).toInt(&okBlue);
        if (!okRed || !okBlue || red < 5 || red > 100 || blue < 5 || blue > 100) {
            err << "keyglow: white takes two numbers from 5 to 100, red then blue (now " << s.whiteRed << " "
                << s.whiteBlue << ")\n";
            return 1;
        }
        s.whiteRed = red;
        s.whiteBlue = blue;
    } else if (command == "color") {
        if (!QColor(value).isValid()) {
            err << "keyglow: '" << value << "' is not a colour (try #ff8800)\n";
            return 1;
        }
        s.color = QColor(value);
    } else {
        return -1;
    }
    s.identify = -1;
    if (!s.save(path)) {
        err << "keyglow: could not write " << path << "\n";
        return 1;
    }
    // Make sure something is listening; a second streamer exits on its own.
    QProcess streamer;
    streamer.setProgram(QCoreApplication::applicationFilePath());
    streamer.setArguments({"daemon"});
    streamer.setStandardInputFile(QProcess::nullDevice());
    streamer.setStandardOutputFile(QProcess::nullDevice());
    streamer.setStandardErrorFile(QProcess::nullDevice());
    streamer.startDetached();
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && QByteArray(argv[1]) == "daemon")
        return runDaemon(argc, argv);
    if (argc > 1 && (QByteArray(argv[1]) == "--version" || QByteArray(argv[1]) == "version")) {
        QTextStream(stdout) << "keyglow " << KEYGLOW_VERSION << "\n";
        return 0;
    }
    if (argc > 1) {
        QCoreApplication app(argc, argv);
        const int result = runCommand(app.arguments().mid(1));
        if (result >= 0)
            return result;
        QTextStream(stderr) << "keyglow: unknown command '" << argv[1]
                            << "'. Try: list, status, toggle, on, off, next, prev, preset, brightness, speed, color, white\n";
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("keyglow");
    app.setApplicationVersion(KEYGLOW_VERSION);

    // Matches the window to keyglow.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("keyglow");
    app.setWindowIcon(QIcon::fromTheme("keyglow"));

    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::presetsChanged);
    engine.addImageProvider("scene", new SceneImages(&backend)); // the engine owns it
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // For checking the layout without a screen:
    //   QT_QPA_PLATFORM=offscreen KEYGLOW_SHOT=out.png KEYGLOW_SIZE=626x688 keyglow
    if (const QString shot = qEnvironmentVariable("KEYGLOW_SHOT"); !shot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QStringList size = qEnvironmentVariable("KEYGLOW_SIZE").split('x');
        if (window && size.size() == 2)
            window->resize(size[0].toInt(), size[1].toInt());
        QTimer::singleShot(1500, window, [window, shot] {
            window->grabWindow().save(shot);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
