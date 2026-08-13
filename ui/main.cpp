// WisprFlow UI - floating pill + debug/settings windows
#include <QApplication>
#include <QLockFile>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>

#include "pill.h"
#include "wfclient.h"
#include "debugwin.h"
#include "settingswin.h"
#include "json.h"

static QString sockPath() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) return runtime + "/wispr-flow.sock";
    return QString("/tmp/wispr-flow-%1.sock").arg(QCoreApplication::applicationPid());
}

static QString daemonPath() {
    return QCoreApplication::applicationDirPath() + "/wispr-flow";
}

// KWin on Wayland ignores Qt's "always on top" flag - it needs a window rule.
// Write one for us on first run (keep above, skip taskbar).
static void ensureKWinRule() {
    const QString path = QDir::homePath() + "/.config/kwinrulesrc";
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QString content = QString::fromUtf8(f.readAll());
        f.close();
        if (content.contains("WisprFlow pill")) return; // already there
    }
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    int idx = 1;
    {   // pick the next free section number
        QFile r(path);
        if (r.open(QIODevice::ReadOnly)) {
            const QString c = QString::fromUtf8(r.readAll());
            QRegularExpression re("\\[([0-9]+)\\]");
            auto it = re.globalMatch(c);
            while (it.hasNext()) idx = qMax(idx, it.next().captured(1).toInt() + 1);
        }
    }
    QTextStream out(&f);
    out << "\n[" << idx << "]\n"
        << "above=true\naboveRule=2\n"
        << "Description=WisprFlow pill\n"
        << "matchrule=1\n"
        << "skiptaskbar=true\nskiptaskbarRule=2\n"
        << "wmclass=wispr-flow-ui\nwmclassmatch=1\n";
    f.close();
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
        // another UI instance is running; bring it to front is complex - just exit
        return 0;
    }

    WfClient client;
    Pill pill;
    DebugWin debug(&client);
    SettingsWin settings;

    ensureKWinRule(); // keep the pill above everything (KWin needs a rule on Wayland)

    const QString sock = sockPath();
    const QString dpath = daemonPath();
    bool daemonStartedByUs = false;

    QObject::connect(&client, &WfClient::hello, &pill, [&](const QByteArray &line) {
        pill.setBackend(jsonGetString(line, "backend"));
        pill.setState(jsonGetString(line, "state"));
    });
    QObject::connect(&client, &WfClient::stateChanged, &pill, &Pill::setState);
    QObject::connect(&client, &WfClient::level, &pill, &Pill::addLevel);
    QObject::connect(&client, &WfClient::done, &debug, &DebugWin::showDone);
    QObject::connect(&client, &WfClient::done, &pill, [&](const DoneInfo &info) {
        pill.setTranscribed(info.text);
        if (!info.ok) pill.setState("idle"); // show normal state again
    });

    // daemon not running? start it (autostart-safe)
    QTimer::singleShot(800, &client, [&] {
        if (!client.isConnected() && QFile::exists(dpath) && !daemonStartedByUs) {
            daemonStartedByUs = true;
            QProcess::startDetached(dpath, QStringList());
        }
    });

    // pill interactions
    QObject::connect(&pill, &Pill::openDebug, &debug, [&] { debug.show(); debug.raise(); debug.activateWindow(); });
    QObject::connect(&pill, &Pill::openSettings, &settings, [&] { settings.show(); settings.raise(); settings.activateWindow(); });
    QObject::connect(&pill, &Pill::requestRestartDaemon, &app, [&] {
        QProcess::startDetached("pkill", {"-x", "wispr-flow"});
        QTimer::singleShot(600, &app, [&] {
            if (QFile::exists(dpath)) QProcess::startDetached(dpath, QStringList());
        });
    });
    QObject::connect(&pill, &Pill::requestQuit, &app, &QApplication::quit);
    QObject::connect(&settings, &SettingsWin::restartDaemon, &app, [&] {
        pill.requestRestartDaemon();
    });

    client.start(sock);
    pill.show();
    return app.exec();
}
