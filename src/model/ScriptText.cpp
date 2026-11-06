#include "ScriptText.h"

namespace ScriptText {

namespace {

// Advances over a string literal starting at s[i] (a quote). Returns the index after it.
int skipString(const QString &s, int i)
{
    const QChar q = s[i];
    const bool triple = s.mid(i, 3) == QString(3, q);
    i += triple ? 3 : 1;
    while (i < s.size()) {
        if (s[i] == QLatin1Char('\\')) {
            i += 2;
            continue;
        }
        if (triple) {
            if (s.mid(i, 3) == QString(3, q))
                return i + 3;
        } else if (s[i] == q) {
            return i + 1;
        }
        ++i;
    }
    return s.size();
}

bool isOpen(QChar c) { return c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{'); }
bool isClose(QChar c) { return c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}'); }

} // namespace

int findTopLevel(const QString &s, const QString &chars, int from)
{
    int depth = 0;
    int i = from;
    while (i < s.size()) {
        const QChar c = s[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            i = skipString(s, i);
            continue;
        }
        if (depth == 0 && chars.contains(c))
            return i;
        if (isOpen(c))
            ++depth;
        else if (isClose(c))
            depth = qMax(0, depth - 1);
        ++i;
    }
    return -1;
}

int findTopLevelToken(const QString &s, const QString &token, int from)
{
    int depth = 0;
    int i = from;
    while (i < s.size()) {
        const QChar c = s[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            i = skipString(s, i);
            continue;
        }
        if (depth == 0 && s.mid(i, token.size()) == token)
            return i;
        if (isOpen(c))
            ++depth;
        else if (isClose(c))
            depth = qMax(0, depth - 1);
        ++i;
    }
    return -1;
}

QStringList splitTopLevel(const QString &s, QChar sep)
{
    QStringList out;
    int depth = 0;
    int start = 0;
    int i = 0;
    while (i < s.size()) {
        const QChar c = s[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            i = skipString(s, i);
            continue;
        }
        if (isOpen(c) || c == QLatin1Char('<'))
            ++depth;
        else if (isClose(c) || c == QLatin1Char('>'))
            depth = qMax(0, depth - 1);
        else if (depth == 0 && c == sep) {
            out << s.mid(start, i - start).trimmed();
            start = i + 1;
        }
        ++i;
    }
    const QString last = s.mid(start).trimmed();
    if (!last.isEmpty() || !out.isEmpty())
        out << last;
    return out;
}

int matchBracket(const QString &s, int open)
{
    int depth = 0;
    int i = open;
    while (i < s.size()) {
        const QChar c = s[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            i = skipString(s, i);
            continue;
        }
        if (isOpen(c))
            ++depth;
        else if (isClose(c)) {
            --depth;
            if (depth == 0)
                return i + 1;
        }
        ++i;
    }
    return -1;
}

bool unquote(const QString &in, QString *out)
{
    QString s = in.trimmed();
    if (s.startsWith(QLatin1Char('&')) || s.startsWith(QLatin1Char('^')) || s.startsWith(QLatin1Char('@')))
        s = s.mid(1);
    if (s.size() < 2)
        return false;
    const QChar q = s[0];
    if (q != QLatin1Char('"') && q != QLatin1Char('\''))
        return false;
    const int end = skipString(s, 0);
    if (end != s.size())
        return false;
    const bool triple = s.startsWith(QString(3, q)) && s.size() >= 6;
    QString body = triple ? s.mid(3, s.size() - 6) : s.mid(1, s.size() - 2);
    QString res;
    for (int i = 0; i < body.size(); ++i) {
        if (body[i] == QLatin1Char('\\') && i + 1 < body.size()) {
            const QChar e = body[++i];
            switch (e.unicode()) {
            case 'n': res += QLatin1Char('\n'); break;
            case 't': res += QLatin1Char('\t'); break;
            case 'r': res += QLatin1Char('\r'); break;
            default: res += e;
            }
        } else {
            res += body[i];
        }
    }
    if (out)
        *out = res;
    return true;
}

bool isIdentStart(QChar c)
{
    return c.isLetter() || c == QLatin1Char('_');
}

bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

QString readIdent(const QString &s, int &pos)
{
    const int start = pos;
    if (pos < s.size() && isIdentStart(s[pos])) {
        ++pos;
        while (pos < s.size() && isIdentChar(s[pos]))
            ++pos;
    }
    return s.mid(start, pos - start);
}

void skipSpaces(const QString &s, int &pos)
{
    while (pos < s.size() && s[pos].isSpace())
        ++pos;
}

} // namespace ScriptText
