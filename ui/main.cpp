// WisprFlow UI - system tray indicator + Plasma OSD feedback.
// No floating windows: the state lives in the tray (shell-owned, can never be
// hidden behind other windows) and transient feedback is drawn by the shell's
// own OSD overlay (same as volume/brightness popups).
#include <QApplication>
#include <QLockFile>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <unistd.h>

#include "tray.h"
#include "pill.h"
#include "osd.h"
#include "wfclient.h"
#include "debugwin.h"
#include "settingswin.h"
#include "json.h"

static QString sockPath() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) return runtime + "/wispr-flow.sock";
    // must match the daemon's fallback (which uses getuid()), NOT the UI's pid
    return QString("/tmp/wispr-flow-%1.sock").arg(getuid());
}

static QString daemonPath() {
    return QCoreApplication::applicationDirPath() + "/wispr-flow";
}

// one line, at most `max` chars - for the OSD popup
static QString oneLine(const QString &in, int max) {
    QString s = in.simplified();
    if (s.size() > max) s = s.left(max).trimmed() + "…";
    return s;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("wispr-flow-ui");
    app.setQuitOnLastWindowClosed(false);

    // single instance
    const QString lockPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ui.lock";
    QDir().mkpath(QFileInfo(lockPath).dir().path());
    QLockFile lock(lockPath);
    if (!lock.tryLock(100)) {
        // another UI instance is running - just exit
        return 0;
    }

    WfClient client;
    Osd osd;
    Tray tray;
    Pill pill; // fallback: only used where there is no system tray
    bool useTray = QSystemTrayIcon::isSystemTrayAvailable();
    QString currentState = "offline";
    if (!useTray) pill.show();
    else tray.setVisible(true);
    // keep tray/pill state in sync initially
    if (useTray) tray.setState(currentState); else pill.setState(currentState);

    // poll for tray availability (e.g. panel starts late)
    auto *trayPoll = new QTimer(&app);
    trayPoll->setInterval(2000);
    QObject::connect(trayPoll, &QTimer::timeout, &app, [&] {
        bool now = QSystemTrayIcon::isSystemTrayAvailable();
        if (now == useTray) return;
        useTray = now;
        if (useTray) {
            // tray became available: hide pill, show tray with current state
            pill.hide();
            tray.setVisible(true);
            tray.setState(currentState);
        } else {
            tray.setVisible(false);
            pill.setState(currentState);
            pill.show();
            pill.raise();
        }
    });
    trayPoll->start();

    DebugWin debug(&client);
    SettingsWin settings;

    bool daemonStartedByUs = false;

    // --- state -> indicator + Plasma OSD (the shell draws the popup) --------
    auto setState = [&](const QString &s) {
        currentState = s;
        if (useTray) tray.setState(s); else pill.setState(s);
    };
    auto setBackend = [&](const QString &s) {
        if (useTray) tray.setBackend(s); else pill.setBackend(s);
    };
    auto setTranscribed = [&](const QString &s) {
        if (useTray) tray.setTranscribed(s); else pill.setTranscribed(s);
    };
    auto addLevel = [&](int v) { if (!useTray) pill.addLevel(v); };

    QObject::connect(&client, &WfClient::hello, &app, [&](const QByteArray &line) {
        setBackend(jsonGetString(line, "backend"));
        setState(jsonGetString(line, "state"));
    });
    QObject::connect(&client, &WfClient::stateChanged, &app, [&](const QString &state) {
        setState(state);
        if (state == "recording")
            osd.show("audio-input-microphone", "Listening…  release Ctrl+Win to finish");
        else if (state == "processing")
            osd.show("process-working", "Transcribing…");
        // "idle" needs no popup - the result popup comes from the done event
    });
    QObject::connect(&client, &WfClient::disconnected, &app, [&] {
        setState("offline"); // daemon died -> indicator reflects it immediately
    });
    QObject::connect(&client, &WfClient::level, &app, [&](int v) { addLevel(v); });
    QObject::connect(&client, &WfClient::done, &debug, &DebugWin::showDone);
    QObject::connect(&client, &WfClient::done, &app, [&](const DoneInfo &info) {
        setTranscribed(info.text);
        if (info.ok) {
            if (info.text.isEmpty())
                osd.show("dialog-ok-apply", "Dictation inserted");
            else
                osd.show("dialog-ok-apply", "✓ " + oneLine(info.text, 100));
        } else if (info.noSpeech) {
            osd.show("dialog-warning", "No speech detected");
        } else {
            osd.show("dialog-error", "Dictation failed: " + oneLine(info.error, 80));
        }
        if (!info.ok) setState("idle"); // show normal state again
    });

    // daemon not running? start it (autostart-safe)
    QTimer::singleShot(800, &client, [&] {
        if (!client.isConnected() && QFile::exists(daemonPath()) && !daemonStartedByUs) {
            daemonStartedByUs = true;
            QProcess::startDetached(daemonPath(), QStringList());
        }
    });

    // --- indicator interactions ----------------------------------------------
    auto openDebug = [&] { debug.show(); debug.raise(); debug.activateWindow(); };
    auto openSettings = [&] { settings.show(); settings.raise(); settings.activateWindow(); };
    auto restartDaemon = [&] {
        QProcess::startDetached("pkill", {"-x", "wispr-flow"});
        QTimer::singleShot(600, &app, [&] {
            if (QFile::exists(daemonPath())) QProcess::startDetached(daemonPath(), QStringList());
        });
    };
    // connect both tray and pill regardless of initial useTray, so switching works
    QObject::connect(&tray, &Tray::openDebug, &app, openDebug);
    QObject::connect(&tray, &Tray::openSettings, &app, openSettings);
    QObject::connect(&tray, &Tray::requestRestartDaemon, &app, restartDaemon);
    QObject::connect(&tray, &Tray::requestQuit, &app, &QApplication::quit);
    QObject::connect(&pill, &Pill::openDebug, &app, openDebug);
    QObject::connect(&pill, &Pill::openSettings, &app, openSettings);
    QObject::connect(&pill, &Pill::requestRestartDaemon, &app, restartDaemon);
    QObject::connect(&pill, &Pill::requestQuit, &app, &QApplication::quit);
    QObject::connect(&settings, &SettingsWin::restartDaemon, &app, restartDaemon);

    client.start(sockPath());
    return app.exec();
}
