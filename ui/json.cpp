#include "json.h"

static QString unescape(const QByteArray &raw) {
    QString out;
    out.reserve(raw.size());
    bool esc = false;
    for (char c : raw) {
        if (esc) {
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            default: out += QLatin1Char(c); break;
            }
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else {
            out += QLatin1Char(c);
        }
    }
    return out;
}

static QByteArray findValue(const QByteArray &line, const char *key) {
    // find  "key":"...
    const QByteArray pat = QByteArray("\"") + key + "\":\"";
    int i = line.indexOf(pat);
    if (i < 0) return QByteArray();
    i += pat.size();
    QByteArray out;
    bool esc = false;
    for (; i < line.size(); i++) {
        char c = line[i];
        if (esc) { out += c; esc = false; }
        else if (c == '\\') esc = true;
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

QString jsonGetString(const QByteArray &line, const char *key) {
    return unescape(findValue(line, key));
}

bool jsonGetBool(const QByteArray &line, const char *key) {
    const QByteArray pat = QByteArray("\"") + key + "\":";
    int i = line.indexOf(pat);
    if (i < 0) return false;
    return line.mid(i + pat.size()).startsWith("true");
}

long long jsonGetInt(const QByteArray &line, const char *key) {
    const QByteArray pat = QByteArray("\"") + key + "\":";
    int i = line.indexOf(pat);
    if (i < 0) return 0;
    i += pat.size();
    return line.mid(i).toLongLong();
}
