#include "wfclient.h"
#include "json.h"
#include <QTimer>

WfClient::WfClient(QObject *parent) : QObject(parent) {
    connect(&m_sock, &QLocalSocket::connected, this, &WfClient::onConnected);
    connect(&m_sock, &QLocalSocket::disconnected, this, &WfClient::onDisconnected);
    connect(&m_sock, &QLocalSocket::readyRead, this, &WfClient::onReadyRead);
}

void WfClient::start(const QString &sockPath) {
    m_path = sockPath;
    retryConnect();
}

void WfClient::retryConnect() {
    if (!m_connected) m_sock.connectToServer(m_path);
}

void WfClient::onConnected() {
    m_connected = true;
    m_buf.clear();
    sendCommand("{\"cmd\":\"hello\"}");
}

void WfClient::onDisconnected() {
    m_connected = false;
    emit disconnected();
    QTimer::singleShot(1500, this, &WfClient::retryConnect);
}

void WfClient::sendCommand(const QByteArray &cmd) {
    if (m_connected) {
        m_sock.write(cmd + "\n");
        m_sock.flush();
    }
}

void WfClient::onReadyRead() {
    m_buf += m_sock.readAll();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        const QString type = jsonGetString(line, "type");
        if (type == "hello") {
            emit hello(line);
        } else if (type == "state") {
            emit stateChanged(jsonGetString(line, "state"));
        } else if (type == "level") {
            emit level((int)jsonGetInt(line, "v"));
        } else if (type == "done") {
            DoneInfo info;
            info.ok = jsonGetBool(line, "ok");
            info.text = jsonGetString(line, "text");
            info.recMs = jsonGetInt(line, "rec_ms");
            info.whisperMs = jsonGetInt(line, "whisper_ms");
            info.noSpeech = jsonGetBool(line, "no_speech");
            info.error = jsonGetString(line, "error");
            info.path = jsonGetString(line, "path");
            emit done(info);
        }
    }
}
