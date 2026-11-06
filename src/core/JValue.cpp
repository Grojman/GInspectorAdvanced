#include "JValue.h"

#include <cmath>

static const JValue s_undefined;

qint64 JValue::toInt(qint64 def) const
{
    if (m_type != Number)
        return def;
    return qint64(std::llround(m_num));
}

bool JValue::isIntegral() const
{
    return m_type == Number && std::isfinite(m_num) && std::floor(m_num) == m_num;
}

int JValue::size() const
{
    if (m_type == Array)
        return int(m_arr.size());
    if (m_type == Object)
        return int(m_obj.size());
    return 0;
}

const JValue &JValue::at(int i) const
{
    if (m_type != Array || i < 0 || i >= int(m_arr.size()))
        return s_undefined;
    return m_arr[size_t(i)];
}

JValue &JValue::operator[](int i)
{
    return m_arr[size_t(i)];
}

void JValue::append(const JValue &v)
{
    if (m_type != Array) {
        *this = array();
    }
    m_arr.push_back(v);
}

void JValue::insert(int i, const JValue &v)
{
    if (m_type != Array)
        *this = array();
    i = qBound(0, i, int(m_arr.size()));
    m_arr.insert(m_arr.begin() + i, v);
}

void JValue::removeAt(int i)
{
    if (m_type == Array && i >= 0 && i < int(m_arr.size()))
        m_arr.erase(m_arr.begin() + i);
}

int JValue::indexOfKey(const QString &key) const
{
    if (m_type != Object)
        return -1;
    for (size_t i = 0; i < m_obj.size(); ++i)
        if (m_obj[i].key == key)
            return int(i);
    return -1;
}

bool JValue::contains(const QString &key) const
{
    return indexOfKey(key) >= 0;
}

JValue JValue::value(const QString &key) const
{
    const int i = indexOfKey(key);
    return i < 0 ? JValue() : m_obj[size_t(i)].value;
}

const JValue &JValue::ref(const QString &key) const
{
    const JValue *v = find(key);
    return v ? *v : s_undefined;
}

JValue *JValue::find(const QString &key)
{
    const int i = indexOfKey(key);
    return i < 0 ? nullptr : &m_obj[size_t(i)].value;
}

const JValue *JValue::find(const QString &key) const
{
    const int i = indexOfKey(key);
    return i < 0 ? nullptr : &m_obj[size_t(i)].value;
}

void JValue::set(const QString &key, const JValue &v)
{
    if (m_type != Object)
        *this = object();
    if (v.isUndefined()) {
        remove(key);
        return;
    }
    const int i = indexOfKey(key);
    if (i >= 0)
        m_obj[size_t(i)].value = v;
    else
        m_obj.push_back(JMember{key, v});
}

bool JValue::remove(const QString &key)
{
    const int i = indexOfKey(key);
    if (i < 0)
        return false;
    m_obj.erase(m_obj.begin() + i);
    return true;
}

bool JValue::renameKey(const QString &from, const QString &to)
{
    const int i = indexOfKey(from);
    if (i < 0 || (from != to && contains(to)))
        return false;
    m_obj[size_t(i)].key = to;
    return true;
}

QStringList JValue::keys() const
{
    QStringList out;
    for (const JMember &m : m_obj)
        out << m.key;
    return out;
}

bool JValue::operator==(const JValue &o) const
{
    if (m_type != o.m_type)
        return false;
    switch (m_type) {
    case Undefined:
    case Null:
        return true;
    case Bool:
        return m_bool == o.m_bool;
    case Number:
        return m_num == o.m_num;
    case String:
        return m_str == o.m_str;
    case Array:
        return m_arr == o.m_arr;
    case Object:
        if (m_obj.size() != o.m_obj.size())
            return false;
        for (size_t i = 0; i < m_obj.size(); ++i) {
            // Key order does not matter for equality.
            const JValue *other = o.find(m_obj[i].key);
            if (!other || !(*other == m_obj[i].value))
                return false;
        }
        return true;
    }
    return false;
}

static QString numberPreview(double d)
{
    return QString::number(d, 'g', 10);
}

QString JValue::preview(int maxLen) const
{
    QString s;
    switch (m_type) {
    case Undefined: s = QStringLiteral("—"); break;
    case Null: s = QStringLiteral("null"); break;
    case Bool: s = m_bool ? QStringLiteral("true") : QStringLiteral("false"); break;
    case Number: s = numberPreview(m_num); break;
    case String: s = m_str; break;
    case Array: {
        QStringList parts;
        bool simple = true;
        for (const JValue &v : m_arr) {
            if (v.isContainer()) { simple = false; break; }
            parts << v.preview(maxLen);
        }
        s = simple ? QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")))
                   : QStringLiteral("[%1 elements]").arg(m_arr.size());
        break;
    }
    case Object: s = QStringLiteral("{%1 keys}").arg(m_obj.size()); break;
    }
    if (maxLen > 0 && s.size() > maxLen)
        s = s.left(maxLen - 1) + QChar(0x2026);
    return s;
}

QString JValue::typeName() const
{
    switch (m_type) {
    case Undefined: return QStringLiteral("absent");
    case Null: return QStringLiteral("null");
    case Bool: return QStringLiteral("bool");
    case Number: return isIntegral() ? QStringLiteral("integer") : QStringLiteral("number");
    case String: return QStringLiteral("string");
    case Array: return QStringLiteral("array");
    case Object: return QStringLiteral("object");
    }
    return QString();
}

JPath operator+(const JPath &p, const PathSeg &s)
{
    JPath out = p;
    out.append(s);
    return out;
}

QString pathToString(const JPath &path)
{
    QString out;
    for (const PathSeg &s : path) {
        if (s.isIndex())
            out += QStringLiteral("[%1]").arg(s.index);
        else {
            if (!out.isEmpty())
                out += QLatin1Char('.');
            out += s.key;
        }
    }
    return out;
}

QString pathPattern(const JPath &path)
{
    QString out;
    for (const PathSeg &s : path) {
        if (s.isIndex())
            out += QStringLiteral("[*]");
        else {
            if (!out.isEmpty())
                out += QLatin1Char('.');
            out += s.key;
        }
    }
    return out;
}

JValue getAt(const JValue &root, const JPath &path)
{
    const JValue *cur = &root;
    for (const PathSeg &s : path) {
        if (s.isIndex()) {
            if (!cur->isArray() || s.index >= cur->size())
                return JValue();
            cur = &cur->at(s.index);
        } else {
            if (!cur->isObject())
                return JValue();
            cur = cur->find(s.key);
            if (!cur)
                return JValue();
        }
    }
    return *cur;
}

static bool setAtImpl(JValue &cur, const JPath &path, int depth, const JValue &v)
{
    if (depth == path.size()) {
        cur = v;
        return true;
    }
    const PathSeg &s = path[depth];
    const bool last = depth == path.size() - 1;
    if (s.isIndex()) {
        if (!cur.isArray() || s.index >= cur.size())
            return false;
        if (last && v.isUndefined()) {
            cur.removeAt(s.index);
            return true;
        }
        return setAtImpl(cur[s.index], path, depth + 1, v);
    }
    if (!cur.isObject()) {
        if (v.isUndefined())
            return true; // nothing to remove
        cur = JValue::object();
    }
    if (last) {
        cur.set(s.key, v);
        return true;
    }
    JValue *child = cur.find(s.key);
    if (!child) {
        if (v.isUndefined())
            return true;
        // Create the intermediate container matching the next segment.
        const bool nextIsIndex = path[depth + 1].isIndex();
        if (nextIsIndex)
            return false; // cannot index into a missing array
        cur.set(s.key, JValue::object());
        child = cur.find(s.key);
    }
    return setAtImpl(*child, path, depth + 1, v);
}

bool setAt(JValue &root, const JPath &path, const JValue &v)
{
    return setAtImpl(root, path, 0, v);
}
