#include "json.h"

static inline int skipWhitespace(const QByteArray &s, int pos) {
    while (pos < s.size()) {
        char c = s[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
        else break;
    }
    return pos;
}

static QString unescape(const QByteArray &raw) {
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ) {
        char c = raw[i];
        if (c != '\\') { out += QLatin1Char(c); ++i; continue; }
        ++i;
        if (i >= raw.size()) break;
        char e = raw[i++];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'u': {
            if (i + 4 <= raw.size()) {
                bool ok = false;
                uint code = raw.mid(i, 4).toUInt(&ok, 16);
                if (ok) {
                    out += QChar(static_cast<ushort>(code));
                    i += 4;
                } else {
                    out += QLatin1Char('u');
                }
            } else {
                out += QLatin1Char('u');
            }
            break;
        }
        default: out += QLatin1Char(e); break;
        }
    }
    return out;
}

// returns position of value after ":" (already whitespace-skipped), or -1
static int findValuePos(const QByteArray &line, const char *key) {
    const QByteArray pat = QByteArray("\"") + key + "\"";
    int idx = line.indexOf(pat);
    if (idx < 0) return -1;
    int pos = idx + pat.size();
    pos = skipWhitespace(line, pos);
    if (pos >= line.size() || line[pos] != ':') return -1;
    ++pos;
    pos = skipWhitespace(line, pos);
    return pos;
}

QString jsonGetString(const QByteArray &line, const char *key) {
    int pos = findValuePos(line, key);
    if (pos < 0 || pos >= line.size() || line[pos] != '"') return QString();
    ++pos; // skip opening quote
    QByteArray raw;
    raw.reserve(64);
    bool esc = false;
    for (; pos < line.size(); ++pos) {
        char c = line[pos];
        if (esc) {
            raw += '\\';
            raw += c;
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            break;
        } else {
            raw += c;
        }
    }
    return unescape(raw);
}

bool jsonGetBool(const QByteArray &line, const char *key) {
    int pos = findValuePos(line, key);
    if (pos < 0) return false;
    const QByteArray rest = line.mid(pos);
    if (rest.startsWith("true")) return true;
    return false;
}

long long jsonGetInt(const QByteArray &line, const char *key) {
    int pos = findValuePos(line, key);
    if (pos < 0) return 0;
    int end = pos;
    if (end < line.size() && line[end] == '-') ++end;
    while (end < line.size() && line[end] >= '0' && line[end] <= '9') ++end;
    bool ok = false;
    long long v = line.mid(pos, end - pos).toLongLong(&ok);
    return ok ? v : 0;
}
