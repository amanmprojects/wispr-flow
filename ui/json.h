#pragma once
// tiny line-JSON helpers (newline-delimited objects from the daemon socket)
#include <QByteArray>
#include <QString>

// value of "key" as a string ("" if absent), with basic unescaping
QString jsonGetString(const QByteArray &line, const char *key);
bool jsonGetBool(const QByteArray &line, const char *key);
long long jsonGetInt(const QByteArray &line, const char *key);
