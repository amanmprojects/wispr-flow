#pragma once
// debug window: shows the transcript, replay the exact recording used,
// retranscribe it (to find out if a cut ending is capture or model)
#include <QWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include "wfclient.h"

class DebugWin : public QWidget {
    Q_OBJECT
public:
    explicit DebugWin(WfClient *client);

public slots:
    void showDone(const DoneInfo &info);

signals:
    void retranscribeRequested();

private:
    WfClient *m_client;
    QPlainTextEdit *m_text;
    QLabel *m_info;
    QPushButton *m_retr = nullptr;
    QPushButton *m_play = nullptr;
    DoneInfo m_last;
};
