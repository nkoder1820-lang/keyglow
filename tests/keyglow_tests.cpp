#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "device.h"
#include "keyinput.h"
#include "lighting.h"
#include "sceneimages.h"
#include "theme.h"
#include "translator.h"

class KeyglowTests : public QObject {
    Q_OBJECT

private slots:
    void frameMatchesTheProtocol()
    {
        QVector<QColor> colors(Lighting::Leds, QColor(Qt::black));
        colors[0] = QColor("#112233");
        colors[125] = QColor("#aabbcc");
        const QByteArray f = Lighting::frame(colors);
        QCOMPARE(f.size(), 520);
        QCOMPARE(f.left(8), QByteArray::fromHex("0608000001007a01"));
        QCOMPARE(f.mid(8, 3), QByteArray::fromHex("112233"));
        QCOMPARE(f.mid(8 + 125 * 3, 3), QByteArray::fromHex("aabbcc"));
    }

    void guessedLayoutIsTheArtemis()
    {
        KeyboardLayout layout;
        QVERIFY(!layout.taught());
        QCOMPARE(layout.keys().size(), 68);
        QCOMPARE(layout.width(), 16.0);
        QCOMPARE(layout.height(), 5.0);
        QCOMPARE(layout.keys().first().label, QString("Esc"));
    }

    void untaughtLightsTheWholeMatrixTaughtOnlyRealKeys()
    {
        Settings s;
        s.preset = "solid";
        s.color = QColor("#ff8000");
        s.brightness = 50;
        Engine engine;
        engine.setLayout(KeyboardLayout());
        const auto all = engine.render(s, 0);
        for (const QColor &c : all)
            QCOMPARE((c).rgb(), QColor(128, 64, 0).rgb());

        // Two keys and a light bar: only those three light up.
        engine.setLayout(KeyboardLayout::fromPairs({{1, 0x29}, {7, 0x1E}, {40, -1}}));
        const auto some = engine.render(s, 0);
        QCOMPARE((some.at(1)).rgb(), QColor(128, 64, 0).rgb());
        QCOMPARE((some.at(40)).rgb(), QColor(128, 64, 0).rgb());
        QCOMPARE((some.at(0)).rgb(), QColor(Qt::black).rgb());

        s.power = false;
        QCOMPARE((engine.render(s, 0).at(1)).rgb(), QColor(Qt::black).rgb());
        s.identify = 9;
        QCOMPARE((engine.render(s, 0).at(9)).rgb(), QColor(Qt::white).rgb());
        QVERIFY(engine.render(s, 0).at(1).blue() < 60); // the rest only glows dimly
    }

    void everyPresetRenders()
    {
        Engine engine;
        engine.setLayout(KeyboardLayout());
        Settings s;
        for (const Preset &p : Engine::presets()) {
            s.preset = p.id;
            engine.press(0x04, 1000);
            const auto colors = engine.render(s, 1100);
            QCOMPARE(colors.size(), Lighting::Leds);
            for (const QColor &c : colors)
                QVERIFY2(c.isValid(), qPrintable(p.id));
        }
    }

    void typingHeatGlowsWhereYouType()
    {
        Engine engine;
        KeyboardLayout layout;
        engine.setLayout(layout);
        Settings s;
        s.preset = "heat";
        const int a = layout.ledForUsage(0x04), p = layout.ledForUsage(0x13);
        const QColor cold = engine.render(s, 5000).at(a);
        engine.press(0x04, 5000);
        const auto hot = engine.render(s, 5050);
        QVERIFY(hot.at(a).red() > cold.red() + 100);
        QCOMPARE(hot.at(p), cold);
        QCOMPARE(engine.render(s, 12000).at(a), cold);
    }

    void layoutSavesAndUsesRealWidthsWhenItMatches()
    {
        QTemporaryDir dir;
        // A full teach result shifted one matrix row down, with other columns.
        QVector<QPair<int, int>> pairs;
        const int counts[] = {15, 15, 14, 14, 10};
        for (int r = 0; r < 5; ++r)
            for (int j = 0; j < counts[r]; ++j)
                pairs.append({(j + (r == 4 && j > 3 ? 3 : 0)) * 6 + r + 1, 0x04 + (r * 15 + j) % 60});
        const KeyboardLayout taught = KeyboardLayout::fromPairs(pairs);
        QVERIFY(taught.taught());
        QCOMPARE(taught.keys().size(), 68);
        QCOMPARE(taught.width(), 16.0);
        QVERIFY(taught.save(dir.filePath("layout.json")));
        const KeyboardLayout back = KeyboardLayout::load(dir.filePath("layout.json"));
        QCOMPARE(back.keys().size(), 68);
        QCOMPARE(back.keys().at(13).w, 2.0); // backspace
        QVERIFY(!KeyboardLayout::load(dir.filePath("missing.json")).taught());
    }

    void settingsRoundTripAndSurviveGarbage()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("sub/config.json");
        Settings s;
        s.preset = "ember";
        s.speed = 80;
        s.power = false;
        s.custom[3] = QColor("#010203");
        QVERIFY(s.save(path));
        const Settings back = Settings::load(path);
        QCOMPARE(back.preset, QString("ember"));
        QCOMPARE(back.speed, 80);
        QVERIFY(!back.power);
        QCOMPARE(back.custom.at(3), QColor("#010203"));

        QFile bad(path);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{\"preset\":\"nonsense\",\"brightness\":900,\"identify\":5000}");
        bad.close();
        QCOMPARE(Settings::load(path).preset, Settings().preset);
        QCOMPARE(Settings::load(path).brightness, 100);
        QCOMPARE(Settings::load(path).identify, Lighting::Leds - 1);
    }

    void jsonForBarsListsEverySceneAndTheCurrentState()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        backend.setPreset("ember");
        backend.setBrightness(40);
        backend.toggleFavorite("nord");

        const QJsonObject list = Cli::scenes(backend);
        const QJsonArray scenes = list.value("scenes").toArray();
        QCOMPARE(scenes.size(), Engine::presets().size());
        QCOMPARE(list.value("categories").toArray().size(), backend.categories().size());
        bool sawNord = false;
        for (const QJsonValue &v : scenes) {
            const QJsonObject scene = v.toObject();
            QVERIFY(!scene.value("name").toString().isEmpty());
            QVERIFY(scene.value("palette").isArray());
            QVERIFY(scene.contains("usesColor") && scene.contains("usesSpeed"));
            for (const QJsonValue &c : scene.value("palette").toArray())
                QVERIFY2(QColor(c.toString()).isValid(), qPrintable(c.toString()));
            if (scene.value("id").toString() == "nord") {
                sawNord = true;
                QVERIFY(scene.value("favorite").toBool());
                QVERIFY(!scene.value("palette").toArray().isEmpty());
            }
        }
        QVERIFY(sawNord);

        const QJsonObject status = Cli::status(backend);
        QCOMPARE(status.value("preset").toString(), QString("ember"));
        QCOMPARE(status.value("name").toString(), QString("Ember"));
        QCOMPARE(status.value("brightness").toInt(), 40);
        QVERIFY(status.value("power").toBool());
        QCOMPARE(status.value("favorites").toArray().size(), 1);
        QVERIFY(QStringList({"ready", "permission", "missing"}).contains(status.value("device").toString()));
    }

    void whiteIsBalancedForTheKeyboardButPureColoursStayFull()
    {
        Settings s; // red and blue at 28% of green
        QCOMPARE(Lighting::balanced(QColor("#ffffff"), s), QColor(71, 255, 71));
        QCOMPARE(Lighting::balanced(QColor("#ff0000"), s), QColor("#ff0000"));
        QCOMPARE(Lighting::balanced(QColor("#0000ff"), s), QColor("#0000ff"));
        QCOMPARE(Lighting::balanced(QColor("#00ff00"), s), QColor("#00ff00"));
        QCOMPARE(Lighting::balanced(QColor("#000000"), s), QColor("#000000"));
        // Magenta has no green to be weak, so it is left alone; yellow loses red.
        QCOMPARE(Lighting::balanced(QColor("#ff00ff"), s), QColor("#ff00ff"));
        QCOMPARE(Lighting::balanced(QColor("#ffff00"), s), QColor(71, 255, 0));
        // Dimmer colours keep their level: the strongest channel is unchanged.
        QCOMPARE(Lighting::balanced(QColor(128, 128, 128), s).green(), 128);
        s.whiteRed = s.whiteBlue = 100;
        QCOMPARE(Lighting::balanced(QColor("#c08040"), s), QColor("#c08040"));
    }

    void renamedScenesAndWhiteBalanceSurviveALoad()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"preset\":\"gotham\",\"favorites\":[\"claude\",\"nord\"],\"whiteRed\":40,\"whiteBlue\":900}");
        f.close();
        const Settings s = Settings::load(path);
        QCOMPARE(s.preset, QString("nightwatch"));
        QCOMPARE(s.favorites, QStringList({"terracotta", "nord"}));
        QCOMPARE(s.whiteRed, 40);
        QCOMPARE(s.whiteBlue, 100);
        QCOMPARE(Settings().whiteRed, 28);
        QVERIFY(s.save(path));
        QVERIFY(Settings::load(path) == s);
    }

    void backendSavesTogglesAndPaints()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        backend.setPreset("ocean");
        backend.toggle();
        QVERIFY(!backend.power());
        backend.setPreset("ocean"); // picking a scene turns the lights on
        QVERIFY(backend.power());
        backend.step(1);
        QCOMPARE(backend.preset(), QString("ember"));
        backend.step(-1);
        QCOMPARE(backend.preset(), QString("ocean"));
        backend.setColor("#00ff00");
        backend.paint(10);
        QCOMPARE(backend.preset(), QString("custom"));
        QTRY_COMPARE(Settings::load(dir.filePath("config.json")).custom.at(10), QColor("#00ff00"));
        QCOMPARE(backend.presets().size(), Engine::presets().size());

        QVERIFY(!backend.autostart());
        backend.setAutostart(true);
        QVERIFY(backend.autostart());
        backend.setAutostart(false);
        QVERIFY(!backend.autostart());
    }

    void everySceneHasANameACategoryAndAThumbnail()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        const QVariantList presets = backend.presets();
        QCOMPARE(presets.size(), Engine::presets().size());
        QVERIFY(presets.size() >= 50);
        QSet<QString> ids, categories;
        for (const QVariant &c : backend.categories())
            categories.insert(c.toMap().value("id").toString());
        SceneImages images(&backend);
        for (const QVariant &v : presets) {
            const QVariantMap p = v.toMap();
            const QString id = p.value("id").toString();
            QVERIFY2(!ids.contains(id), qPrintable(id));
            ids.insert(id);
            QVERIFY2(!p.value("name").toString().isEmpty() && !p.value("blurb").toString().isEmpty(), qPrintable(id));
            QVERIFY2(categories.contains(p.value("category").toString()), qPrintable(id));
            // The card picture has something lit in it (except the hand-back scene).
            QSize size;
            const QImage image = images.requestImage(id + "/ff8800/3/0/ffd60a", &size, {});
            QCOMPARE(size, QSize(352, 110));
            int lit = 0;
            for (int y = 0; y < image.height(); y += 4)
                for (int x = 0; x < image.width(); x += 4)
                    lit += qGray(image.pixel(x, y)) > 24;
            QVERIFY2(lit > 20, qPrintable(id));
        }
    }

    void favouritesAndOnboardingAreRemembered()
    {
        QTemporaryDir dir;
        {
            Backend backend(dir.path(), false);
            QVERIFY(!backend.onboarded());
            backend.toggleFavorite("terracotta");
            backend.toggleFavorite("reactor");
            backend.toggleFavorite("reactor");
            backend.toggleFavorite("no-such-scene");
            backend.setPreset("terracotta");
            QVERIFY(backend.current().value("favorite").toBool());
            QCOMPARE(backend.current().value("name").toString(), QString("Terracotta"));
            backend.finishOnboarding();
            QVERIFY(backend.onboarded());
        }
        const Settings saved = Settings::load(dir.filePath("config.json"));
        QCOMPARE(saved.favorites, QStringList{"terracotta"});
        QVERIFY(saved.onboarded);
    }

    void teachingWalksTheBoardAndSkipsDeadRows()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        backend.startTeaching();
        QVERIFY(backend.teaching());
        const auto shown = [&] { return Settings::load(dir.filePath("config.json")).identify; };
        QCOMPARE(shown(), 0);

        // A pretend board: matrix rows 0-4, columns 0-14, with the Artemis's
        // key counts. Answer every light the way a person would.
        const int counts[] = {15, 15, 14, 14, 10};
        int usage = 0x04, steps = 0;
        while (backend.teaching() && steps++ < 200) {
            const int led = shown(), column = led / 6, row = led % 6;
            QVERIFY2(column == 0 || row < 5, "a dead row was tried after the first column");
            QTest::qWait(190); // past the key-release guard
            if (row < 5 && column < counts[row])
                backend.teachPress(usage++);
            else
                backend.teachNothing();
        }
        QVERIFY(!backend.teaching());
        QVERIFY(backend.taught());
        QCOMPARE(backend.keys().size(), 68);
        QCOMPARE(backend.boardWidth(), 16.0);
        QCOMPARE(shown(), -1);
        QVERIFY(steps < 90);
    }

    // Answers quick sync the way a person at a board with this wiring would.
    static QVector<QPair<int, int>> syncAgainst(const QVector<QVector<int>> &columns, int offset,
                                                 const QVector<int> &corners, int *answers, bool *failed)
    {
        const KeyboardLayout guess; // gives each slot's usage, row by row
        QHash<int, int> usageAt;    // light -> what the key above it sends
        int index = 0;
        for (int r = 0; r < columns.size(); ++r)
            for (int j = 0; j < columns[r].size(); ++j, ++index) {
                const bool corner = r < 4 && j == columns[r].size() - 1;
                usageAt.insert(columns[r][j] * 6 + r + offset, corner ? corners[r] : guess.keys().at(index).usage);
            }
        QuickSync sync;
        while (!sync.failed() && sync.nextLed() >= 0 && sync.answers() < 60) {
            const int led = sync.nextLed();
            if (!usageAt.contains(led))
                sync.nothing(led);
            else if (usageAt[led] == 0)
                sync.silentKey(led);
            else
                sync.pressed(led, usageAt[led]);
        }
        *answers = sync.answers();
        *failed = sync.failed();
        QVector<QPair<int, int>> truth;
        for (auto it = usageAt.begin(); it != usageAt.end(); ++it)
            truth.append({it.key(), it.value()});
        std::sort(truth.begin(), truth.end());
        auto result = sync.result();
        std::sort(result.begin(), result.end());
        return result == truth ? result : QVector<QPair<int, int>>();
    }

    void quickSyncSolvesDifferentWirings_data()
    {
        QTest::addColumn<QVector<QVector<int>>>("columns");
        QTest::addColumn<int>("offset");
        auto seq = [](int from, int count) { QVector<int> v; for (int i = 0; i < count; ++i) v << from + i; return v; };
        // Packed: every row uses consecutive columns.
        QTest::newRow("packed") << QVector<QVector<int>>{seq(0, 15), seq(0, 15), seq(0, 14), seq(0, 14), seq(0, 10)} << 0;
        // By position: wide keys leave gaps, corner keys share the last column.
        QTest::newRow("by position, rows 1-5")
            << QVector<QVector<int>>{seq(0, 14) << 15, seq(0, 14) << 15, seq(0, 12) << 13 << 15,
                                     QVector<int>{0} + seq(2, 10) << 13 << 14 << 15,
                                     QVector<int>{0, 1, 2, 6, 10, 11, 12, 13, 14, 15}}
            << 1;
        QTest::newRow("mixed")
            << QVector<QVector<int>>{seq(0, 13) << 14 << 15, seq(0, 15), seq(0, 12) << 13 << 14,
                                     QVector<int>{0} + seq(1, 10) << 12 << 13 << 14,
                                     QVector<int>{0, 1, 2, 5, 9, 10, 11, 12, 13, 14}}
            << 0;
    }

    void quickSyncSolvesDifferentWirings()
    {
        QFETCH(QVector<QVector<int>>, columns);
        QFETCH(int, offset);
        int answers = 0;
        bool failed = true;
        // Corner keys that are not what the guess assumes: Del, Home, PgUp, PgDn.
        const auto result = syncAgainst(columns, offset, {0x4C, 0x4A, 0x4B, 0x4E}, &answers, &failed);
        QVERIFY(!failed);
        QCOMPARE(result.size(), 68);
        QVERIFY2(answers <= 24, qPrintable(QString::number(answers)));
        qInfo() << "answers needed:" << answers;
        const KeyboardLayout layout = KeyboardLayout::fromPairs(result);
        QCOMPARE(layout.width(), 16.0);
        QCOMPARE(layout.keys().at(14).label, QString("Del")); // learned, not assumed
    }

    void quickSyncGivesUpOnAStrangeBoard()
    {
        // A board where none of the expected lights exist.
        QuickSync sync;
        for (int i = 0; i < 40 && sync.nextLed() >= 0; ++i)
            sync.nothing(sync.nextLed());
        QVERIFY(sync.failed());
        QVERIFY(sync.answers() < 30);
        QCOMPARE(sync.nextLed(), -1);
    }

    void backendQuickSyncSavesTheLayout()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        backend.startQuickSync();
        QVERIFY(backend.teachQuick());
        const auto shown = [&] { return Settings::load(dir.filePath("config.json")).identify; };
        const KeyboardLayout guess;
        QHash<int, int> usageAt;
        for (const Key &k : guess.keys())
            usageAt.insert(k.led, k.usage);
        int steps = 0;
        while (backend.teaching() && steps++ < 60) {
            QTest::qWait(190);
            const int led = shown();
            if (!usageAt.contains(led)) backend.teachNothing();
            else if (usageAt[led] == 0) backend.teachSilent(true);
            else backend.teachPress(usageAt[led]);
        }
        QVERIFY(!backend.teaching());
        QVERIFY(backend.taught());
        QVERIFY(!backend.notice().isEmpty());
        QCOMPARE(KeyboardLayout::load(dir.filePath("layout.json")).keys().size(), 68);
        QCOMPARE(shown(), -1);

        // A board that makes no sense drops into the full walk instead.
        backend.startQuickSync();
        for (int i = 0; i < 40 && backend.teachQuick(); ++i)
            backend.teachNothing();
        QVERIFY(backend.teaching());
        QVERIFY(!backend.teachQuick());
        backend.cancelTeaching();
    }

    void teachingBackAndDuplicates()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        backend.startTeaching();
        QTest::qWait(190);
        backend.teachPress(0x29);
        QCOMPARE(backend.teachFound(), 1);
        QTest::qWait(190);
        backend.teachPress(0x29); // same key again: refused, with a note
        QCOMPARE(backend.teachFound(), 1);
        QVERIFY(!backend.teachNote().isEmpty());
        backend.teachBack();
        QCOMPARE(backend.teachFound(), 0);
        backend.cancelTeaching();
        QVERIFY(!backend.taught());
    }

    void keyReportsAreParsed()
    {
        // Boot report: left shift held, A and Enter down.
        QCOMPARE(KeyInput::parse(QByteArray::fromHex("0200042800000000")), (QSet<int>{0xE1, 0x04, 0x28}));
        // N-key report: id 4, then one bit per usage from 0x04.
        QCOMPARE(KeyInput::parse(QByteArray::fromHex("040100000000000000000000000000") + QByteArray(1, '\0')),
                 (QSet<int>{0x04}));
        QVERIFY(KeyInput::parse(QByteArray::fromHex("0201")).isEmpty());
    }

    void devicePicksTheLightingInterface()
    {
        QTemporaryDir dir;
        auto node = [&](const QString &name, const QByteArray &uevent, const QByteArray &descriptor) {
            QDir(dir.path()).mkpath(name + "/device");
            write(dir.filePath(name + "/device/uevent"), uevent);
            write(dir.filePath(name + "/device/report_descriptor"), descriptor);
        };
        const QByteArray id = "HID_ID=0003:0000258A:0000010C\n";
        node("hidraw0", id, QByteArray::fromHex("05010906a101"));
        node("hidraw1", "HID_ID=0003:00001532:00000098\n", QByteArray::fromHex("0600ff8506"));
        QCOMPARE(Device::findPath(dir.path()), QString());
        node("hidraw3", id, QByteArray::fromHex("0600ff0901a1018506"));
        QCOMPARE(Device::findPath(dir.path()), QString("/dev/hidraw3"));
        QCOMPARE(Device::findAllPaths(dir.path()), (QStringList{"/dev/hidraw0", "/dev/hidraw3"}));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(KeyglowTests)
#include "keyglow_tests.moc"
