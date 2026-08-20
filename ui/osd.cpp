#include "osd.h"
#include <QTimer>
#include <QIcon>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QStandardPaths>
#include <QProcess>
#include <QSystemTrayIcon>

static void fallbackNotify(const QString &text) {
    const QString exe = QStandardPaths::findExecutable("notify-send");
    if (!exe.isEmpty()) {
        // notify-send exists — use it; startDetached returns false only if fork fails,
        // still try tray fallback if it somehow fails
        bool ok = QProcess::startDetached(exe,
            {"-a", "WisprFlow", "-t", "3000", "WisprFlow", text});
        if (ok) return;
    }
    if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()) {
        // Use a temporary tray icon to show the message. It needs to be visible.
        auto *tray = new QSystemTrayIcon();
        tray->setIcon(QIcon::fromTheme("audio-input-microphone"));
        tray->show();
        tray->showMessage("WisprFlow", text, QSystemTrayIcon::Information, 3000);
        // delete later after message timeout
        QTimer::singleShot(4000, tray, [tray]{ tray->hide(); tray->deleteLater(); });
        return;
    }
    // last resort: try notify-send even if not found via findExecutable (maybe in PATH)
    QProcess::startDetached("notify-send",
        {"-a", "WisprFlow", "-t", "3000", "WisprFlow", text});
}

void Osd::show(const QString &icon, const QString &text) {
    if (!QDBusConnection::sessionBus().isConnected()) {
        fallbackNotify(text);
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.plasmashell"),
        QStringLiteral("/org/kde/osdService"),
        QStringLiteral("org.kde.osdService"),
        QStringLiteral("showText"));
    msg << icon << text;

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, 2000);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, text] {
        w->deleteLater();
        // Any non-ReplyMessage means the OSD service is unavailable or timed out
        if (w->isError()) {
            fallbackNotify(text);
            return;
        }
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            fallbackNotify(text);
            return;
        }
        // Also treat timeout explicitly (isError already covers it, but be explicit)
        // QDBusError::AccessDenied, etc. all go through isError
    });
}
