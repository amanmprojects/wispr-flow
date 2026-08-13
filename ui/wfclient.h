#pragma once
// client for the wispr-flow daemon socket
#include <QObject>
#include <QByteArray>
#include <QLocalSocket>

struct DoneInfo {
    bool ok = false;
    QString text;
    long long recMs = 0;
    long long whisperMs = 0;
    bool noSpeech = false;
    QString error;
    QString path;
    QString state; // what the daemon was doing (for info)
};

class WfClient : public QObject {
    Q_OBJECT
public:
    explicit WfClient(QObject *parent = nullptr);
    void start(const QString &sockPath);
    void sendCommand(const QByteArray &cmd);
    bool isConnected() const { return m_connected; }

signals:
    void hello(const QByteArray &line);
    void stateChanged(const QString &state); // idle | recording | processing
    void level(int v);
    void done(const DoneInfo &info);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void retryConnect();

private:
    QLocalSocket m_sock;
    QString m_path;
    bool m_connected = false;
    QByteArray m_buf;
};
