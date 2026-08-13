#pragma once
// settings window: edits ~/.config/wispr-flow/config (preserving other keys)
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>

class SettingsWin : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWin();

signals:
    void restartDaemon();

private:
    QString configPath() const;
    void load();
    bool save();

    QLineEdit *m_language;
    QLineEdit *m_source;
    QComboBox *m_insertMode;
    QLineEdit *m_pasteCombo;
    QSpinBox *m_tailMs;
    QSpinBox *m_minMs;
    QCheckBox *m_beep;
    QCheckBox *m_notify;
    QCheckBox *m_capitalize;
    QCheckBox *m_trimTrailing;
};
