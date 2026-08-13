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

DebugWin::DebugWin(WfClient *client) : m_client(client) {
    setWindowTitle("WisprFlow — last dictation");
    resize(560, 340);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setPlaceholderText("Nothing transcribed yet — hold Ctrl+Win and speak.");

    m_info = new QLabel(this);
    m_info->setTextFormat(Qt::RichText);
    m_info->setWordWrap(true);

    auto *play = new QPushButton("▶ Replay recording", this);
    auto *retr = new QPushButton("↻ Retranscribe", this);
    retr->setToolTip("Runs the model again on the exact recording used — "
                     "if the ending differs, the cut is in the recording, not the model");
    auto *copy = new QPushButton("Copy text", this);
    auto *folder = new QPushButton("Open folder", this);

    auto *btns = new QHBoxLayout;
    btns->addWidget(play);
    btns->addWidget(retr);
    btns->addWidget(copy);
    btns->addWidget(folder);
    btns->addStretch();

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_text, 1);
    lay->addWidget(m_info);
    lay->addLayout(btns);

    connect(play, &QPushButton::clicked, this, [this] {
        if (m_last.path.isEmpty()) return;
        if (!QProcess::startDetached("paplay", QStringList{m_last.path})) {
            QMessageBox::warning(this, "WisprFlow",
                                 "Could not start paplay — install pulseaudio-utils.");
        }
    });
    connect(retr, &QPushButton::clicked, this, [this] {
        if (m_last.path.isEmpty()) return;
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
    if (!info.text.isEmpty()) m_text->setPlainText(info.text);

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
