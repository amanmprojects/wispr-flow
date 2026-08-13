#include "osd.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QProcess>

void Osd::show(const QString &icon, const QString &text) {
    // Plasma 6 exposes its native OSD over DBus (org.kde.osdService.showText).
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.plasmashell"),
        QStringLiteral("/org/kde/osdService"),
        QStringLiteral("org.kde.osdService"),
        QStringLiteral("showText"));
    msg << icon << text;

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, text] {
        w->deleteLater();
        if (w->reply().type() == QDBusMessage::ErrorMessage) {
            // not Plasma: fall back to a short notification
            QProcess::startDetached("notify-send",
                {"-a", "WisprFlow", "-t", "3000", "WisprFlow", text});
        }
    });
}
