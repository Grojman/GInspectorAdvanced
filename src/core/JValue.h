#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <vector>

struct JMember;

// An ordered JSON value. Unlike QJsonValue it keeps the order of object keys and
// remembers whether a number was written as an integer ("2") or a float ("2.0"),
// so files can be saved back without reformatting the user's data.
class JValue
{
public:
    enum Type { Undefined, Null, Bool, Number, String, Array, Object };
    // How a container was laid out in the source file, so saving keeps the user's style.
    enum Layout : quint8 { LayoutAuto, LayoutInline, LayoutInlinePadded, LayoutMultiline };

    JValue() = default; // Undefined: "this key is absent"
    JValue(std::nullptr_t) : m_type(Null) {}
    JValue(bool b) : m_type(Bool), m_bool(b) {}
    JValue(int i) : m_type(Number), m_num(i), m_intLexeme(true) {}
    JValue(qint64 i) : m_type(Number), m_num(double(i)), m_intLexeme(true) {}
    JValue(double d, bool intLexeme = false) : m_type(Number), m_num(d), m_intLexeme(intLexeme) {}
    JValue(const QString &s) : m_type(String), m_str(s) {}
    JValue(const char *s) : m_type(String), m_str(QString::fromUtf8(s)) {}

    static JValue null() { return JValue(nullptr); }
    static JValue array() { JValue v; v.m_type = Array; return v; }
    static JValue object() { JValue v; v.m_type = Object; return v; }
    static JValue integer(qint64 i) { return JValue(i); }
    static JValue real(double d) { return JValue(d, false); }

    Type type() const { return m_type; }
    bool isUndefined() const { return m_type == Undefined; }
    bool isNull() const { return m_type == Null; }
    bool isBool() const { return m_type == Bool; }
    bool isNumber() const { return m_type == Number; }
    bool isString() const { return m_type == String; }
    bool isArray() const { return m_type == Array; }
    bool isObject() const { return m_type == Object; }
    bool isContainer() const { return m_type == Array || m_type == Object; }

    bool toBool(bool def = false) const { return m_type == Bool ? m_bool : def; }
    double toDouble(double def = 0.0) const { return m_type == Number ? m_num : def; }
    qint64 toInt(qint64 def = 0) const;
    QString toString(const QString &def = QString()) const { return m_type == String ? m_str : def; }
    // True when the number has no fractional part (regardless of how it was written).
    bool isIntegral() const;
    // True when the number was written without '.' or exponent.
    bool hasIntLexeme() const { return m_intLexeme; }
    void setIntLexeme(bool v) { m_intLexeme = v; }

    // Arrays
    int size() const;
    const JValue &at(int i) const;
    JValue &operator[](int i);
    void append(const JValue &v);
    void insert(int i, const JValue &v);
    void removeAt(int i);
    const std::vector<JValue> &elements() const { return m_arr; }
    std::vector<JValue> &elements() { return m_arr; }

    // Objects
    bool contains(const QString &key) const;
    JValue value(const QString &key) const; // Undefined if missing
    // Reference to the member (or to a shared Undefined): safe in range-for loops.
    const JValue &ref(const QString &key) const;
    JValue *find(const QString &key);
    const JValue *find(const QString &key) const;
    void set(const QString &key, const JValue &v); // keeps position, appends if new
    bool remove(const QString &key);
    bool renameKey(const QString &from, const QString &to);
    QStringList keys() const;
    int indexOfKey(const QString &key) const;
    const std::vector<JMember> &members() const { return m_obj; }
    std::vector<JMember> &members() { return m_obj; }

    Layout layout() const { return m_layout; }
    void setLayout(Layout l) { m_layout = l; }

    bool operator==(const JValue &o) const;
    bool operator!=(const JValue &o) const { return !(*this == o); }

    // Short human readable rendering used in cards and messages.
    QString preview(int maxLen = 40) const;
    QString typeName() const;

private:
    Type m_type = Undefined;
    bool m_bool = false;
    double m_num = 0.0;
    bool m_intLexeme = false;
    Layout m_layout = LayoutAuto;
    QString m_str;
    std::vector<JValue> m_arr;
    std::vector<JMember> m_obj;
};

struct JMember
{
    QString key;
    JValue value;
};

// A path inside a JSON value: object keys and array indices.
struct PathSeg
{
    QString key;
    int index = -1;
    static PathSeg k(const QString &key) { PathSeg s; s.key = key; return s; }
    static PathSeg i(int idx) { PathSeg s; s.index = idx; return s; }
    bool isIndex() const { return index >= 0; }
    bool operator==(const PathSeg &o) const { return key == o.key && index == o.index; }
};
using JPath = QList<PathSeg>;

JPath operator+(const JPath &p, const PathSeg &s);
QString pathToString(const JPath &path);
// Same as pathToString but with array indices replaced by "*": used to remember
// expanded sections independently of which element is shown.
QString pathPattern(const JPath &path);
JValue getAt(const JValue &root, const JPath &path);
// Sets the value at path, creating intermediate objects. An Undefined value removes the key.
bool setAt(JValue &root, const JPath &path, const JValue &v);
