#include "settingswin.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QMap>

SettingsWin::SettingsWin() {
    setWindowTitle("WisprFlow — settings");
    resize(420, 380);

    m_language = new QLineEdit(this);
    m_language->setPlaceholderText("en, de, zh, … (or auto)");

    m_source = new QLineEdit(this);
    m_source->setPlaceholderText("(default microphone)");

    m_insertMode = new QComboBox(this);
    m_insertMode->addItems({"paste", "type"});

    m_pasteCombo = new QLineEdit(this);
    m_tailMs = new QSpinBox(this);
    m_tailMs->setRange(0, 2000);
    m_tailMs->setSuffix(" ms");
    m_minMs = new QSpinBox(this);
    m_minMs->setRange(0, 5000);
    m_minMs->setSuffix(" ms");

    m_beep = new QCheckBox("Feedback beeps", this);
    m_beep->setAccessibleDescription("Play a short beep when dictation starts and stops");
    m_notify = new QCheckBox("Desktop notifications", this);
    m_notify->setAccessibleDescription("Show desktop notification when dictation is inserted or fails");
    m_capitalize = new QCheckBox("Capitalize first letter", this);
    m_capitalize->setAccessibleDescription("Automatically capitalize the first letter of dictated text");
    m_trimTrailing = new QCheckBox("Trim long trailing silence (may clip soft endings)", this);
    m_trimTrailing->setAccessibleDescription("Trim trailing silence from the recording to reduce latency");

    auto *form = new QFormLayout;
    auto *langLabel = new QLabel("Language:", this);
    langLabel->setBuddy(m_language);
    form->addRow(langLabel, m_language);
    auto *sourceLabel = new QLabel("Audio source:", this);
    sourceLabel->setBuddy(m_source);
    form->addRow(sourceLabel, m_source);
    auto *modeLabel = new QLabel("Insert mode:", this);
    modeLabel->setBuddy(m_insertMode);
    form->addRow(modeLabel, m_insertMode);
    auto *pasteLabel = new QLabel("Paste shortcut:", this);
    pasteLabel->setBuddy(m_pasteCombo);
    form->addRow(pasteLabel, m_pasteCombo);
    auto *tailLabel = new QLabel("Capture tail:", this);
    tailLabel->setBuddy(m_tailMs);
    form->addRow(tailLabel, m_tailMs);
    auto *minLabel = new QLabel("Min. recording:", this);
    minLabel->setBuddy(m_minMs);
    form->addRow(minLabel, m_minMs);
    form->addRow("", m_beep);
    form->addRow("", m_notify);
    form->addRow("", m_capitalize);
    form->addRow("", m_trimTrailing);

    auto *hint = new QLabel(
        "<span style='color:#9ca3af'>Audio source: leave empty for the default mic, "
        "or use <b>monitor</b> to dictate from speakers. Full list: "
        "<code>pactl list sources short</code></span>", this);
    hint->setWordWrap(true);

    auto *saveBtn = new QPushButton("Save", this);
    auto *restart = new QPushButton("Restart daemon", this);
    auto *openCfg = new QPushButton("Open config file…", this);
    auto *cancel = new QPushButton("Close", this);

    auto *btns = new QHBoxLayout;
    btns->addWidget(saveBtn);
    btns->addWidget(restart);
    btns->addWidget(openCfg);
    btns->addStretch();
    btns->addWidget(cancel);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(hint);
    lay->addStretch();
    lay->addLayout(btns);

    connect(saveBtn, &QPushButton::clicked, this, [this] { SettingsWin::save(); });
    connect(cancel, &QPushButton::clicked, this, &QWidget::close);
    connect(restart, &QPushButton::clicked, this, [this] { emit restartDaemon(); });
    connect(openCfg, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(configPath()));
    });

    load();
}

QString SettingsWin::configPath() const {
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty()) return xdg + "/wispr-flow/config";
    return QDir::homePath() + "/.config/wispr-flow/config";
}

void SettingsWin::load() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QTextStream in(&f);
    QMap<QString, QString> kv;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const int eq = line.indexOf('=');
        if (eq > 0) kv[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
    }
    auto get = [&](const char *k, const QString &dflt) {
        return kv.contains(k) ? kv[k] : dflt;
    };
    m_language->setText(get("language", "en"));
    m_source->setText(get("source", ""));
    const QString mode = get("insert_mode", "paste");
    m_insertMode->setCurrentIndex(mode == "type" ? 1 : 0);
    m_pasteCombo->setText(get("paste_combo", "ctrl+v"));
    m_tailMs->setValue(get("stop_tail_ms", "350").toInt());
    m_minMs->setValue(get("min_audio_ms", "300").toInt());
    auto isTrue = [&](const char *k, bool dflt) {
        const QString v = get(k, dflt ? "true" : "false");
        return v == "true" || v == "1" || v == "yes";
    };
    m_beep->setChecked(isTrue("beep", true));
    m_notify->setChecked(isTrue("notify", true));
    m_capitalize->setChecked(isTrue("capitalize", true));
    m_trimTrailing->setChecked(isTrue("trim_trailing_silence", false));
}

bool SettingsWin::save() {
    QMap<QString, QString> updates;
    updates["language"] = m_language->text().trimmed();
    updates["source"] = m_source->text().trimmed();
    updates["insert_mode"] = m_insertMode->currentText();
    updates["paste_combo"] = m_pasteCombo->text().trimmed();
    updates["stop_tail_ms"] = QString::number(m_tailMs->value());
    updates["min_audio_ms"] = QString::number(m_minMs->value());
    updates["beep"] = m_beep->isChecked() ? "true" : "false";
    updates["notify"] = m_notify->isChecked() ? "true" : "false";
    updates["capitalize"] = m_capitalize->isChecked() ? "true" : "false";
    updates["trim_trailing_silence"] = m_trimTrailing->isChecked() ? "true" : "false";

    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).dir().path());

    // Read existing file to preserve unknown keys/comments
    QStringList kept;
    QSet<QString> written;
    QFile existing(path);
    if (existing.exists() && existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&existing);
        while (!in.atEnd()) {
            QString line = in.readLine();
            const int eq = line.indexOf('=');
            if (eq > 0) {
                const QString key = line.left(eq).trimmed();
                if (updates.contains(key)) {
                    kept << key + " = " + updates[key];
                    written.insert(key);
                    continue;
                }
            }
            kept << line;
        }
        existing.close();
    }
    for (auto it = updates.cbegin(); it != updates.cend(); ++it)
        if (!written.contains(it.key()))
            kept << it.key() + " = " + it.value();

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "WisprFlow", "Cannot write " + path + ": " + f.errorString());
        return false;
    }
    QTextStream out(&f);
    out << kept.join('\n') << '\n';
    if (!f.commit()) {
        QMessageBox::warning(this, "WisprFlow", "Cannot commit " + path + ": " + f.errorString());
        return false;
    }
    return true;
}
