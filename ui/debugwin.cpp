#include "debugwin.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileInfo>

DebugWin::DebugWin(WfClient *client) : m_client(client) {
    setWindowTitle("WisprFlow — last dictation");
    resize(560, 340);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setPlaceholderText("Nothing transcribed yet — hold Ctrl+Win and speak.");

    m_info = new QLabel(this);
    m_info->setTextFormat(Qt::RichText);
    m_info->setWordWrap(true);

    m_play = new QPushButton("▶ Replay recording", this);
    m_retr = new QPushButton("↻ Retranscribe", this);
    m_retr->setToolTip("Runs the model again on the exact recording used — "
                     "if the ending differs, the cut is in the recording, not the model");
    auto *copy = new QPushButton("Copy text", this);
    auto *folder = new QPushButton("Open folder", this);

    // disable retranscribe when offline
    m_retr->setEnabled(m_client->isConnected());
    if (!m_client->isConnected()) m_retr->setToolTip("Daemon offline — reconnecting…");
    connect(m_client, &WfClient::hello, this, [this](const QByteArray&) {
        m_retr->setEnabled(true);
        m_retr->setToolTip("Runs the model again on the exact recording used — "
                         "if the ending differs, the cut is in the recording, not the model");
    });
    connect(m_client, &WfClient::disconnected, this, [this]{
        m_retr->setEnabled(false);
        m_retr->setToolTip("Daemon offline — reconnecting…");
    });

    auto *btns = new QHBoxLayout;
    btns->addWidget(m_play);
    btns->addWidget(m_retr);
    btns->addWidget(copy);
    btns->addWidget(folder);
    btns->addStretch();

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_text, 1);
    lay->addWidget(m_info);
    lay->addLayout(btns);

    connect(m_play, &QPushButton::clicked, this, [this] {
        if (m_last.path.isEmpty()) return;
        auto tryPlay = [&](const QString &exe) -> bool {
            if (exe.isEmpty()) return false;
            return QProcess::startDetached(exe, QStringList{m_last.path});
        };
        QString paplay = QStandardPaths::findExecutable("paplay");
        if (!paplay.isEmpty() && tryPlay(paplay)) return;
        QString pwplay = QStandardPaths::findExecutable("pw-play");
        if (!pwplay.isEmpty() && tryPlay(pwplay)) return;
        QString aplay = QStandardPaths::findExecutable("aplay");
        if (!aplay.isEmpty() && tryPlay(aplay)) return;
        // fallback: try bare names via PATH even if not found by findExecutable
        if (tryPlay("paplay")) return;
        if (tryPlay("pw-play")) return;
        if (tryPlay("aplay")) return;
        QMessageBox::warning(this, "WisprFlow",
                             "Could not start audio player — install pulseaudio-utils, pipewire or alsa-utils.");
    });
    connect(m_retr, &QPushButton::clicked, this, [this] {
        if (m_last.path.isEmpty()) return;
        if (!m_client->isConnected()) {
            m_retr->setToolTip("Daemon offline — cannot retranscribe");
            return;
        }
        m_client->sendCommand("{\"cmd\":\"retranscribe\"}");
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        if (!m_last.text.isEmpty()) QGuiApplication::clipboard()->setText(m_last.text);
    });
    connect(folder, &QPushButton::clicked, this, [this] {
        if (!m_last.path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_last.path).dir().path()));
    });
}

void DebugWin::showDone(const DoneInfo &info) {
    m_last = info;
    if (info.noSpeech || info.text.isEmpty()) {
        m_text->clear();
    } else {
        m_text->setPlainText(info.text);
    }

    QString html = "<span style='color:#9ca3af'>";
    html += QString("recording <b>%1 s</b> · whisper <b>%2 ms</b> · ")
                .arg(info.recMs / 1000.0, 0, 'f', 1)
                .arg(info.whisperMs);
    html += info.ok ? "<span style='color:#4ade80'>inserted ✓</span>"
                    : QString("<span style='color:#f87171'>%1</span>")
                          .arg(info.noSpeech ? "no speech" : (info.error.isEmpty() ? "failed" : info.error.toHtmlEscaped()));
    if (!info.path.isEmpty())
        html += QString(" · <span style='color:#6b7280'>%1</span>").arg(info.path.toHtmlEscaped());
    html += "</span>";
    m_info->setText(html);
}
