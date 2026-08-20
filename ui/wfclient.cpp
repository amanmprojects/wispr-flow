#include "wfclient.h"
#include "json.h"
#include <QTimer>

WfClient::WfClient(QObject *parent) : QObject(parent) {
    connect(&m_sock, &QLocalSocket::connected, this, &WfClient::onConnected);
    connect(&m_sock, &QLocalSocket::disconnected, this, &WfClient::onDisconnected);
    connect(&m_sock, &QLocalSocket::errorOccurred, this, &WfClient::onError);
    connect(&m_sock, &QLocalSocket::readyRead, this, &WfClient::onReadyRead);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &WfClient::retryConnect);
    m_helloTimer.setSingleShot(true);
    connect(&m_helloTimer, &QTimer::timeout, this, [this] {
        if (!m_helloReceived && m_connected) {
            qWarning("WfClient: no hello reply within 3s — daemon may be stale");
        }
    });
}

void WfClient::start(const QString &sockPath) {
    m_path = sockPath;
    retryConnect();
}

void WfClient::scheduleRetry() {
    if (m_retryTimer.isActive()) return;
    m_retryTimer.start(1500);
}

void WfClient::retryConnect() {
    if (!m_connected) m_sock.connectToServer(m_path);
}

void WfClient::flushQueue() {
    while (!m_queue.isEmpty() && m_connected) {
        const QByteArray cmd = m_queue.dequeue();
        m_sock.write(cmd + "\n");
    }
    if (m_connected) m_sock.flush();
}

void WfClient::onConnected() {
    m_connected = true;
    m_buf.clear();
    m_helloReceived = false;
    m_helloTimer.start(3000);
    sendCommand("{\"cmd\":\"hello\"}");
    flushQueue();
}

void WfClient::onError(QLocalSocket::LocalSocketError) {
    if (!m_connected) {
        scheduleRetry();
    }
}

void WfClient::onDisconnected() {
    m_connected = false;
    m_helloTimer.stop();
    emit disconnected();
    scheduleRetry();
}

void WfClient::sendCommand(const QByteArray &cmd) {
    if (m_connected) {
        m_sock.write(cmd + "\n");
        m_sock.flush();
    } else {
        m_queue.enqueue(cmd);
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
            m_helloReceived = true;
            m_helloTimer.stop();
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
