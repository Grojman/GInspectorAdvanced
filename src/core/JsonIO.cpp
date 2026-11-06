#include "JsonIO.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace {

class Parser
{
public:
    explicit Parser(const QString &text) : m_s(text) {}

    JValue run(JsonParseError *err)
    {
        JValue v;
        skipWs();
        if (!parseValue(v, 0))
            return fail(err);
        skipWs();
        if (m_pos < m_s.size()) {
            error(QStringLiteral("Unexpected trailing characters"));
            return fail(err);
        }
        if (err)
            *err = JsonParseError();
        return v;
    }

private:
    JValue fail(JsonParseError *err)
    {
        if (err) {
            err->message = m_error;
            int line = 1, col = 1;
            for (int i = 0; i < m_errorPos && i < m_s.size(); ++i) {
                if (m_s[i] == QLatin1Char('\n')) { ++line; col = 1; }
                else ++col;
            }
            err->line = line;
            err->column = col;
        }
        return JValue();
    }

    void error(const QString &msg)
    {
        if (m_error.isEmpty()) {
            m_error = msg;
            m_errorPos = m_pos;
        }
    }

    void skipWs()
    {
        while (m_pos < m_s.size()) {
            const QChar c = m_s[m_pos];
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r')
                || c == QChar(0xFEFF))
                ++m_pos;
            else
                break;
        }
    }

    bool match(const char *lit)
    {
        const int n = int(std::strlen(lit));
        if (m_s.mid(m_pos, n) == QLatin1String(lit)) {
            m_pos += n;
            return true;
        }
        return false;
    }

    bool parseValue(JValue &out, int depth)
    {
        if (depth > 512) {
            error(QStringLiteral("Nesting too deep"));
            return false;
        }
        if (m_pos >= m_s.size()) {
            error(QStringLiteral("Unexpected end of file"));
            return false;
        }
        const QChar c = m_s[m_pos];
        if (c == QLatin1Char('{'))
            return parseObject(out, depth);
        if (c == QLatin1Char('['))
            return parseArray(out, depth);
        if (c == QLatin1Char('"')) {
            QString s;
            if (!parseString(s))
                return false;
            out = JValue(s);
            return true;
        }
        if (match("true")) { out = JValue(true); return true; }
        if (match("false")) { out = JValue(false); return true; }
        if (match("null")) { out = JValue::null(); return true; }
        if (c == QLatin1Char('-') || c.isDigit())
            return parseNumber(out);
        error(QStringLiteral("Unexpected character '%1'").arg(c));
        return false;
    }

    bool parseNumber(JValue &out)
    {
        const int start = m_pos;
        bool isInt = true;
        if (m_s[m_pos] == QLatin1Char('-'))
            ++m_pos;
        while (m_pos < m_s.size()) {
            const QChar c = m_s[m_pos];
            if (c.isDigit()) { ++m_pos; continue; }
            if (c == QLatin1Char('.') || c == QLatin1Char('e') || c == QLatin1Char('E')
                || c == QLatin1Char('+') || c == QLatin1Char('-')) {
                isInt = false;
                ++m_pos;
                continue;
            }
            break;
        }
        const QByteArray lex = m_s.mid(start, m_pos - start).toLatin1();
        double d = 0;
        const auto res = std::from_chars(lex.constData(), lex.constData() + lex.size(), d);
        if (res.ec != std::errc() || res.ptr != lex.constData() + lex.size()) {
            m_pos = start;
            error(QStringLiteral("Invalid number '%1'").arg(QString::fromLatin1(lex)));
            return false;
        }
        out = JValue(d, isInt);
        return true;
    }

    bool parseHex4(uint &code)
    {
        if (m_pos + 4 > m_s.size()) {
            error(QStringLiteral("Bad unicode escape"));
            return false;
        }
        bool ok = false;
        code = m_s.mid(m_pos, 4).toUInt(&ok, 16);
        if (!ok) {
            error(QStringLiteral("Bad unicode escape"));
            return false;
        }
        m_pos += 4;
        return true;
    }

    bool parseString(QString &out)
    {
        ++m_pos; // opening quote
        out.clear();
        while (m_pos < m_s.size()) {
            const QChar c = m_s[m_pos++];
            if (c == QLatin1Char('"'))
                return true;
            if (c == QLatin1Char('\\')) {
                if (m_pos >= m_s.size())
                    break;
                const QChar e = m_s[m_pos++];
                switch (e.unicode()) {
                case '"': out += QLatin1Char('"'); break;
                case '\\': out += QLatin1Char('\\'); break;
                case '/': out += QLatin1Char('/'); break;
                case 'b': out += QLatin1Char('\b'); break;
                case 'f': out += QLatin1Char('\f'); break;
                case 'n': out += QLatin1Char('\n'); break;
                case 'r': out += QLatin1Char('\r'); break;
                case 't': out += QLatin1Char('\t'); break;
                case 'u': {
                    uint code = 0;
                    if (!parseHex4(code))
                        return false;
                    out += QChar(char16_t(code));
                    break;
                }
                default:
                    --m_pos;
                    error(QStringLiteral("Invalid escape '\\%1'").arg(e));
                    return false;
                }
            } else if (c == QLatin1Char('\n')) {
                --m_pos;
                error(QStringLiteral("Unterminated string"));
                return false;
            } else {
                out += c;
            }
        }
        error(QStringLiteral("Unterminated string"));
        return false;
    }

    void setLayout(JValue &v, int start) const
    {
        const bool multi = m_s.mid(start, m_pos - start).contains(QLatin1Char('\n'));
        if (multi)
            v.setLayout(JValue::LayoutMultiline);
        else if (start + 1 < m_s.size() && m_s[start + 1] == QLatin1Char(' '))
            v.setLayout(JValue::LayoutInlinePadded);
        else
            v.setLayout(JValue::LayoutInline);
    }

    bool parseArray(JValue &out, int depth)
    {
        const int start = m_pos;
        const bool ok = parseArrayImpl(out, depth);
        if (ok)
            setLayout(out, start);
        return ok;
    }

    bool parseObject(JValue &out, int depth)
    {
        const int start = m_pos;
        const bool ok = parseObjectImpl(out, depth);
        if (ok)
            setLayout(out, start);
        return ok;
    }

    bool parseArrayImpl(JValue &out, int depth)
    {
        ++m_pos;
        out = JValue::array();
        skipWs();
        if (m_pos < m_s.size() && m_s[m_pos] == QLatin1Char(']')) {
            ++m_pos;
            return true;
        }
        while (true) {
            skipWs();
            JValue v;
            if (!parseValue(v, depth + 1))
                return false;
            out.append(v);
            skipWs();
            if (m_pos >= m_s.size()) {
                error(QStringLiteral("Unterminated array"));
                return false;
            }
            const QChar c = m_s[m_pos++];
            if (c == QLatin1Char(']'))
                return true;
            if (c != QLatin1Char(',')) {
                --m_pos;
                error(QStringLiteral("Expected ',' or ']'"));
                return false;
            }
        }
    }

    bool parseObjectImpl(JValue &out, int depth)
    {
        ++m_pos;
        out = JValue::object();
        skipWs();
        if (m_pos < m_s.size() && m_s[m_pos] == QLatin1Char('}')) {
            ++m_pos;
            return true;
        }
        while (true) {
            skipWs();
            if (m_pos >= m_s.size() || m_s[m_pos] != QLatin1Char('"')) {
                error(QStringLiteral("Expected a string key"));
                return false;
            }
            QString key;
            if (!parseString(key))
                return false;
            skipWs();
            if (m_pos >= m_s.size() || m_s[m_pos] != QLatin1Char(':')) {
                error(QStringLiteral("Expected ':'"));
                return false;
            }
            ++m_pos;
            skipWs();
            JValue v;
            if (!parseValue(v, depth + 1))
                return false;
            out.set(key, v); // duplicate keys: last one wins, like Godot
            skipWs();
            if (m_pos >= m_s.size()) {
                error(QStringLiteral("Unterminated object"));
                return false;
            }
            const QChar c = m_s[m_pos++];
            if (c == QLatin1Char('}'))
                return true;
            if (c != QLatin1Char(',')) {
                --m_pos;
                error(QStringLiteral("Expected ',' or '}'"));
                return false;
            }
        }
    }

    const QString &m_s;
    int m_pos = 0;
    QString m_error;
    int m_errorPos = 0;
};

bool allScalars(const JValue &v)
{
    if (v.isArray()) {
        for (const JValue &e : v.elements())
            if (e.isContainer())
                return false;
    } else if (v.isObject()) {
        for (const JMember &m : v.members())
            if (m.value.isContainer())
                return false;
    }
    return true;
}

void writeValue(QString &out, const JValue &v, const JsonIO::WriteOptions &o, int depth, bool isElement);

void writeIndent(QString &out, const JsonIO::WriteOptions &o, int depth)
{
    for (int i = 0; i < depth; ++i)
        out += o.indent;
}

void writeValue(QString &out, const JValue &v, const JsonIO::WriteOptions &o, int depth, bool isElement)
{
    if (!v.isContainer()) {
        out += JsonIO::writeCompact(v);
        return;
    }
    if (v.size() == 0) {
        out += v.isArray() ? QStringLiteral("[]") : QStringLiteral("{}");
        return;
    }
    if (v.layout() == JValue::LayoutInline || v.layout() == JValue::LayoutInlinePadded) {
        out += JsonIO::writeCompact(v);
        return;
    }
    if (v.layout() == JValue::LayoutAuto && depth >= o.expandDepth) {
        const QString compact = JsonIO::writeCompact(v);
        const int room = compact.size() + depth * 4;
        if ((allScalars(v) && room <= o.inlineLimit) || (isElement && room <= o.elementInlineLimit)) {
            out += compact;
            return;
        }
    }
    const bool arr = v.isArray();
    out += arr ? QLatin1Char('[') : QLatin1Char('{');
    out += QLatin1Char('\n');
    const int n = v.size();
    for (int i = 0; i < n; ++i) {
        writeIndent(out, o, depth + 1);
        if (arr) {
            writeValue(out, v.at(i), o, depth + 1, true);
        } else {
            const JMember &m = v.members()[size_t(i)];
            out += JsonIO::quote(m.key);
            out += QStringLiteral(": ");
            writeValue(out, m.value, o, depth + 1, false);
        }
        if (i + 1 < n)
            out += QLatin1Char(',');
        out += QLatin1Char('\n');
    }
    writeIndent(out, o, depth);
    out += arr ? QLatin1Char(']') : QLatin1Char('}');
}

} // namespace

namespace JsonIO {

JValue parse(const QString &text, JsonParseError *error)
{
    Parser p(text);
    return p.run(error);
}

QString quote(const QString &s)
{
    QString out;
    out.reserve(s.size() + 2);
    out += QLatin1Char('"');
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '"': out += QStringLiteral("\\\""); break;
        case '\\': out += QStringLiteral("\\\\"); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        case '\b': out += QStringLiteral("\\b"); break;
        case '\f': out += QStringLiteral("\\f"); break;
        default:
            if (c.unicode() < 0x20)
                out += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
            else
                out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString formatNumber(const JValue &v)
{
    const double d = v.toDouble();
    if (!std::isfinite(d))
        return QStringLiteral("0");
    if (v.hasIntLexeme() && v.isIntegral() && std::fabs(d) < 9.007199254740992e15)
        return QString::number(qint64(d));
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), d);
    QString s = QString::fromLatin1(buf, int(res.ptr - buf));
    if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e')))
        s += QStringLiteral(".0");
    return s;
}

QString writeCompact(const JValue &v)
{
    switch (v.type()) {
    case JValue::Undefined:
    case JValue::Null:
        return QStringLiteral("null");
    case JValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case JValue::Number:
        return formatNumber(v);
    case JValue::String:
        return quote(v.toString());
    case JValue::Array: {
        const bool pad = v.layout() == JValue::LayoutInlinePadded && v.size() > 0;
        QString out = pad ? QStringLiteral("[ ") : QStringLiteral("[");
        for (int i = 0; i < v.size(); ++i) {
            if (i)
                out += QStringLiteral(", ");
            out += writeCompact(v.at(i));
        }
        return out + (pad ? QStringLiteral(" ]") : QStringLiteral("]"));
    }
    case JValue::Object: {
        const bool pad = v.layout() == JValue::LayoutInlinePadded && v.size() > 0;
        QString out = pad ? QStringLiteral("{ ") : QStringLiteral("{");
        bool first = true;
        for (const JMember &m : v.members()) {
            if (!first)
                out += QStringLiteral(", ");
            first = false;
            out += quote(m.key) + QStringLiteral(": ") + writeCompact(m.value);
        }
        return out + (pad ? QStringLiteral(" }") : QStringLiteral("}"));
    }
    }
    return QString();
}

QString write(const JValue &root, const WriteOptions &opts)
{
    QString out;
    writeValue(out, root, opts, 0, false);
    if (opts.trailingNewline)
        out += QLatin1Char('\n');
    return out;
}

QString detectIndent(const QString &text)
{
    int i = text.indexOf(QLatin1Char('\n'));
    while (i >= 0 && i + 1 < text.size()) {
        const QChar c = text[i + 1];
        if (c == QLatin1Char('\t'))
            return QStringLiteral("\t");
        if (c == QLatin1Char(' ')) {
            int n = 0;
            while (i + 1 + n < text.size() && text[i + 1 + n] == QLatin1Char(' '))
                ++n;
            return QString(n, QLatin1Char(' '));
        }
        i = text.indexOf(QLatin1Char('\n'), i + 1);
    }
    return QStringLiteral("\t");
}

} // namespace JsonIO
