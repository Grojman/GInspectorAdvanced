#include "Schema.h"

#include <QRegularExpression>

namespace {

QStringList camelWords(const QString &s)
{
    QStringList out;
    QString cur;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c == QLatin1Char('_')) {
            if (!cur.isEmpty()) out << cur;
            cur.clear();
            continue;
        }
        const bool boundary = c.isUpper() && !cur.isEmpty()
            && (!cur.back().isUpper() || (i + 1 < s.size() && s[i + 1].isLower()));
        if (boundary) {
            out << cur;
            cur.clear();
        }
        cur += c;
    }
    if (!cur.isEmpty())
        out << cur;
    return out;
}

} // namespace

QString Schema::autoTypeName(const QString &className, const QString &baseName)
{
    QStringList words = camelWords(className);
    const QStringList baseWords = camelWords(baseName);
    // Drop the words shared at the end with the base class: AlterStatEffect / ItemEffect -> AlterStat
    int common = 0;
    while (common < words.size() - 1 && common < baseWords.size()
           && words[words.size() - 1 - common] == baseWords[baseWords.size() - 1 - common])
        ++common;
    words = words.mid(0, words.size() - common);
    for (QString &w : words)
        w = w.toLower();
    return words.join(QLatin1Char('_'));
}

Schema::Poly Schema::autoPoly(const QString &baseKey) const
{
    Poly p;
    p.baseKey = baseKey;
    p.automatic = true;
    p.key = m_reg->property(baseKey, QStringLiteral("type")) ? QStringLiteral("_type") : QStringLiteral("type");
    const ClassInfo *base = m_reg->get(baseKey);
    const QString baseName = base ? base->name : baseKey;
    QStringList classes;
    classes << baseKey << m_reg->descendants(baseKey);
    for (const QString &k : classes) {
        const ClassInfo *c = m_reg->get(k);
        if (!c)
            continue;
        QString name = k == baseKey ? autoTypeName(c->name, QString()) : autoTypeName(c->name, baseName);
        if (k == baseKey && !baseName.isEmpty()) {
            // Base itself: strip nothing but still snake_case it ("ItemEffect" -> "item_effect").
            name = autoTypeName(c->name, QString());
        }
        if (name.isEmpty())
            name = c->name.toLower();
        p.types << qMakePair(name, k);
    }
    return p;
}

bool Schema::polyFor(const QString &declaredKey, Poly *out) const
{
    for (const ClassInfo *c : m_reg->chain(declaredKey)) {
        auto it = m_cfg->poly.constFind(c->key);
        if (it != m_cfg->poly.cend()) {
            Poly p;
            p.baseKey = c->key;
            p.key = it->key;
            p.types = it->types;
            if (out)
                *out = p;
            return true;
        }
    }
    if (m_reg->descendants(declaredKey).isEmpty())
        return false;
    if (out)
        *out = autoPoly(declaredKey);
    return true;
}

QString Schema::actualClass(const JValue &obj, const QString &declaredKey, QString *error) const
{
    Poly p;
    if (!obj.isObject() || !polyFor(declaredKey, &p))
        return declaredKey;
    const JValue tv = obj.value(p.key);
    if (tv.isUndefined())
        return declaredKey;
    const QString t = tv.toString();
    for (const auto &e : p.types) {
        if (e.first == t) {
            if (!m_reg->get(e.second)) {
                if (error)
                    *error = QStringLiteral("\"%1\": \"%2\" maps to class %3, which was not found")
                                 .arg(p.key, t, e.second);
                return declaredKey;
            }
            if (e.second != declaredKey && !m_reg->inherits(e.second, declaredKey)) {
                if (error)
                    *error = QStringLiteral("\"%1\": \"%2\" is %3, which does not extend %4")
                                 .arg(p.key, t, displayName(e.second), displayName(declaredKey));
                return declaredKey;
            }
            return e.second;
        }
    }
    if (error) {
        QStringList valid;
        for (const auto &e : p.types)
            if (e.second == declaredKey || m_reg->inherits(e.second, declaredKey))
                valid << e.first;
        *error = QStringLiteral("Unknown \"%1\" value \"%2\" (expected one of: %3)")
                     .arg(p.key, tv.isString() ? t : tv.preview(), valid.join(QStringLiteral(", ")));
    }
    return declaredKey;
}

QString Schema::typeValueFor(const QString &classKey, const QString &declaredKey) const
{
    Poly p;
    if (!polyFor(declaredKey, &p))
        return QString();
    for (const auto &e : p.types)
        if (e.second == classKey)
            return e.first;
    return QString();
}

QStringList Schema::assignableClasses(const QString &declaredKey) const
{
    QStringList out;
    out << declaredKey;
    for (const QString &k : m_reg->descendants(declaredKey))
        out << k;
    return out;
}

JValue Schema::newObject(const QString &classKey, const QString &declaredKey) const
{
    JValue o = JValue::object();
    Poly p;
    if (polyFor(declaredKey, &p)) {
        const QString tv = typeValueFor(classKey, declaredKey);
        // The declared class itself needs no discriminator.
        if (!tv.isEmpty() && classKey != declaredKey)
            o.set(p.key, JValue(tv));
    }
    return o;
}

QString Schema::displayName(const QString &classKey) const
{
    const ClassInfo *c = m_reg->get(classKey);
    if (!c)
        return classKey;
    if (!c->outerKey.isEmpty()) {
        const ClassInfo *o = m_reg->get(c->outerKey);
        return (o ? o->name : c->outerKey) + QLatin1Char('.') + c->name;
    }
    return c->name;
}
