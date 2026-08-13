#pragma once
// transient, shell-drawn feedback: Plasma 6 OSD (the same overlay the system
// uses for volume/brightness popups), with a notify-send fallback for other
// desktops. Drawn by the shell itself -> never behind windows, always themed.
#include <QObject>

class Osd : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void show(const QString &icon, const QString &text);
};
