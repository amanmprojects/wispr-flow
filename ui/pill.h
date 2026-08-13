#pragma once
// the slim floating pill: idle mic -> live waveform while recording ->
// spinner while transcribing
#include <QWidget>
#include <QTimer>
#include <QMenu>
#include <deque>

class Pill : public QWidget {
    Q_OBJECT
public:
    explicit Pill();

    void setState(const QString &state); // idle | recording | processing | offline
    void addLevel(int v);
    void setBackend(const QString &backend) { m_backend = backend; }
    void setTranscribed(const QString &text) { m_lastText = text; }

signals:
    void openDebug();
    void openSettings();
    void requestRestartDaemon();
    void requestQuit();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    QString m_state = "offline";
    std::deque<int> m_levels;
    double m_peak = 0.0;
    int m_spinAngle = 0;
    bool m_dotOn = false;
    QString m_backend;
    QString m_lastText;
    QTimer m_anim;
    QMenu m_menu;
};
