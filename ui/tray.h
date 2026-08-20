#pragma once
// system tray indicator: the persistent state lives in the panel (owned by the
// shell, so it can never be hidden behind windows). Transient feedback during
// dictation goes through the Plasma OSD (see osd.cpp).
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>

class Tray : public QObject {
    Q_OBJECT
public:
    explicit Tray(QObject *parent = nullptr);

    void setVisible(bool v);
    void setState(const QString &state); // idle | recording | processing | offline
    void setBackend(const QString &backend) { m_backend = backend; }
    void setTranscribed(const QString &text) { m_lastText = text; }
signals:
    void openDebug();
    void openSettings();
    void requestRestartDaemon();
    void requestQuit();

private:
    void render(); // repaint the icon for the current state

    QSystemTrayIcon m_tray;
    QMenu m_menu;
    QTimer m_anim;
    QString m_state = "offline";
    QString m_backend;
    QString m_lastText;
    bool m_dotOn = false;   // recording pulse
    int m_frame = 0;        // processing spinner frame
};
