#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "keyinput.h"
#include "lighting.h"

// The window's side: holds the settings, saves every change (the background
// streamer picks the files up), draws the on-screen keyboard, and runs the
// one-time "teach" walk that learns which LED sits under which key.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool power READ power WRITE setPower NOTIFY changed)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY changed)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(int whiteRed READ whiteRed WRITE setWhiteRed NOTIFY changed)
    Q_PROPERTY(int whiteBlue READ whiteBlue WRITE setWhiteBlue NOTIFY changed)
    Q_PROPERTY(bool usesColor READ usesColor NOTIFY changed)
    Q_PROPERTY(bool usesSpeed READ usesSpeed NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY presetsChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(bool onboarded READ onboarded NOTIFY changed)
    Q_PROPERTY(QColor ambient READ ambient NOTIFY frameChanged)
    Q_PROPERTY(int layoutVersion READ layoutVersion NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY layoutChanged)
    Q_PROPERTY(double boardWidth READ boardWidth NOTIFY layoutChanged)
    Q_PROPERTY(double boardHeight READ boardHeight NOTIFY layoutChanged)
    Q_PROPERTY(bool taught READ taught NOTIFY layoutChanged)
    Q_PROPERTY(int frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(int downTick READ downTick NOTIFY downChanged)
    Q_PROPERTY(QString deviceStatus READ deviceStatus NOTIFY deviceStatusChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(bool teaching READ teaching NOTIFY teachChanged)
    Q_PROPERTY(int teachFound READ teachFound NOTIFY teachChanged)
    Q_PROPERTY(int teachExpected READ teachExpected NOTIFY teachChanged)
    Q_PROPERTY(QString teachNote READ teachNote NOTIFY teachChanged)
    Q_PROPERTY(bool teachQuick READ teachQuick NOTIFY teachChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    // The directory is an argument so tests can point it at a temp directory.
    explicit Backend(const QString &configDirectory = {}, bool live = true, QObject *parent = nullptr);
    ~Backend() override;

    bool power() const { return m_settings.power; }
    QString preset() const { return m_settings.preset; }
    QString color() const { return m_settings.color.name(); }
    int brightness() const { return m_settings.brightness; }
    int speed() const { return m_settings.speed; }
    int whiteRed() const { return m_settings.whiteRed; }
    int whiteBlue() const { return m_settings.whiteBlue; }
    bool usesColor() const { return Engine::preset(m_settings.preset).usesColor; }
    bool usesSpeed() const { return Engine::preset(m_settings.preset).usesSpeed; }
    QVariantList presets() const;
    QVariantList categories() const;
    QVariantMap current() const;
    bool onboarded() const { return m_settings.onboarded; }
    QColor ambient() const { return m_ambient; }
    int layoutVersion() const { return m_layoutVersion; }
    const KeyboardLayout &layout() const { return m_layout; }
    const Settings &settings() const { return m_settings; }
    QVariantList keys() const;
    double boardWidth() const { return m_layout.width(); }
    double boardHeight() const { return m_layout.height(); }
    bool taught() const { return m_layout.taught(); }
    int frame() const { return m_frame; }
    int downTick() const { return m_downTick; }
    QString deviceStatus() const { return m_deviceStatus; }
    bool autostart() const;
    bool teaching() const { return m_teachLed >= 0; }
    int teachFound() const;
    int teachExpected() const { return m_quick ? 14 : 68; }
    bool teachQuick() const { return bool(m_quick); }
    QString notice() const { return m_notice; }
    QString teachNote() const { return m_teachNote; }

    void setPower(bool power);
    void setPreset(const QString &preset);
    void setColor(const QString &color);
    void setBrightness(int brightness);
    void setSpeed(int speed);
    void setWhiteRed(int percent);
    void setWhiteBlue(int percent);
    void setAutostart(bool on);

    Q_INVOKABLE QColor colorAt(int led) const { return m_colors.value(led, QColor(Qt::black)); }
    Q_INVOKABLE bool isDown(int usage) const { return usage > 0 && m_input.isDown(usage); }
    Q_INVOKABLE void toggle() { setPower(!power()); }
    Q_INVOKABLE void step(int by);
    Q_INVOKABLE void toggleFavorite(const QString &preset);
    Q_INVOKABLE void finishOnboarding();
    // Colour one LED with the current colour, switching to the painted preset.
    Q_INVOKABLE void paint(int led);
    Q_INVOKABLE void fillCustom();
    // Starts the painted preset from whatever is showing right now.
    Q_INVOKABLE void paintFromCurrent();

    // Teaching: one LED glows at a time and the user presses the key it is under.
    // Quick sync: about a dozen answers, and the rest is worked out.
    Q_INVOKABLE void startQuickSync();
    // The full walk: every light, one by one.
    Q_INVOKABLE void startTeaching();
    Q_INVOKABLE void teachNothing();          // no light came on
    Q_INVOKABLE void teachSilent(bool isKey); // lit, but sends nothing: Fn, or a light that isn't a key
    Q_INVOKABLE void teachBack();
    Q_INVOKABLE void finishTeaching();
    Q_INVOKABLE void cancelTeaching();
    Q_INVOKABLE void forgetLayout();
    // What a press means while teaching. Public so tests can stand in for the keyboard.
    void teachPress(int usage);

signals:
    void changed();
    void presetsChanged();
    void layoutChanged();
    void frameChanged();
    void downChanged();
    void deviceStatusChanged();
    void autostartChanged();
    void teachChanged();
    void noticeChanged();

private:
    void touch();
    // Picks up changes made elsewhere: the bar widget, a key binding, another window.
    void reloadIfChangedOutside();
    void render();
    void refreshDevice();
    void showTeachLed(int led);
    int nextTeachLed(int after) const;
    void teachAdvance();
    void quickAdvance();
    void setNotice(const QString &notice);

    QString m_configPath, m_layoutPath, m_autostartPath;
    Settings m_settings;
    KeyboardLayout m_layout;
    Engine m_engine;
    KeyInput m_input;
    QVector<QColor> m_colors;
    QColor m_ambient = QColor(Qt::black);
    int m_layoutVersion = 0;
    int m_frame = 0, m_downTick = 0;
    QString m_deviceStatus;
    QTimer m_save, m_renderTimer, m_deviceTimer;
    QFileSystemWatcher m_watcher;

    int m_teachLed = -1;
    bool m_teachSweep = false; // second pass over everything the first pass skipped
    QVector<QPair<int, int>> m_teachPairs;
    QVector<int> m_teachTrail; // LEDs already shown, for Back
    QSet<int> m_teachSeen;
    qint64 m_teachShownAt = 0;
    QString m_teachNote;
    std::unique_ptr<QuickSync> m_quick;
    QSet<int> m_quickUsages;
    QString m_notice;
    QTimer m_noticeTimer;
};

#define KEYGLOW_VERSION "1.2.1"

// What `keyglow list --json` and `keyglow status --json` print. A bar widget
// or script reads these instead of scraping the plain-text commands.
namespace Cli {
QJsonObject scenes(const Backend &backend);
QJsonObject status(const Backend &backend);
}
