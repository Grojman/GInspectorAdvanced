#include "Validator.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>

namespace {

void add(QList<Issue> &out, int item, const JPath &path, const QString &msg, bool error = true)
{
    Issue i;
    i.item = item;
    i.path = path;
    i.error = error;
    i.message = msg;
    out << i;
}

QString fmtNum(double d)
{
    return QString::number(d, 'g', 10);
}

const EnumInfo *enumFor(const Schema &s, const TypeRef &t)
{
    return t.enumKey.isEmpty() ? nullptr : s.registry()->enumInfo(t.enumKey);
}

bool optionHasValue(const PropertyHint &h, qint64 v)
{
    for (const auto &o : h.options)
        if (o.second == v)
            return true;
    return false;
}

bool optionHasName(const PropertyHint &h, const QString &n)
{
    for (const auto &o : h.options)
        if (o.first == n)
            return true;
    return false;
}

qint64 flagsMask(const PropertyHint &h)
{
    qint64 m = 0;
    for (const auto &o : h.options)
        m |= o.second;
    return m;
}

} // namespace

bool Validator::inRange(double v, const PropertyHint &h, double *clamped)
{
    double c = v;
    if (h.kind == PropertyHint::Range) {
        if (h.hasMin && !h.orLess && c < h.min)
            c = h.min;
        if (h.hasMax && !h.orGreater && c > h.max)
            c = h.max;
    }
    if (clamped)
        *clamped = c;
    return c == v;
}

bool Validator::matchesFileFilter(const QString &path, const QStringList &filters)
{
    if (filters.isEmpty() || path.isEmpty())
        return true;
    const QString name = path.section(QLatin1Char('/'), -1);
    for (const QString &f : filters) {
        for (const QString &one : f.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(one.trimmed()),
                                        QRegularExpression::CaseInsensitiveOption);
            if (re.match(name).hasMatch())
                return true;
        }
    }
    return false;
}

QList<Issue> Validator::validateItem(const JValue &item, const QString &classKey, int itemIndex,
                                     const QString &keyField) const
{
    QList<Issue> out;
    if (!item.isObject()) {
        add(out, itemIndex, {}, QStringLiteral("Item must be a JSON object, found %1").arg(item.typeName()));
        return out;
    }
    validateObject(item, classKey, itemIndex, {}, out, keyField);
    return out;
}

void Validator::validateObject(const JValue &obj, const QString &declaredKey, int item, const JPath &path,
                               QList<Issue> &out, const QString &keyField) const
{
    QString polyError;
    const QString cls = m_schema.actualClass(obj, declaredKey, &polyError);
    Schema::Poly poly;
    const bool hasPoly = m_schema.polyFor(declaredKey, &poly);
    if (!polyError.isEmpty())
        add(out, item, path + PathSeg::k(poly.key), polyError);

    const ClassRegistry *reg = m_schema.registry();
    for (const JMember &m : obj.members()) {
        const JPath p = path + PathSeg::k(m.key);
        if (hasPoly && m.key == poly.key) {
            if (!m.value.isString())
                add(out, item, p, QStringLiteral("\"%1\" must be a string").arg(m.key));
            continue;
        }
        const ResolvedProperty *rp = reg->property(cls, m.key);
        if (!rp) {
            add(out, item, p, QStringLiteral("Unknown field \"%1\": %2 has no such variable")
                                  .arg(m.key, m_schema.displayName(cls)));
            continue;
        }
        if (path.isEmpty() && !keyField.isEmpty() && m.key == keyField) {
            add(out, item, p, QStringLiteral("\"%1\" is filled from the item key and should not be stored").arg(m.key),
                false);
            continue;
        }
        validateValue(m.value, rp->type, rp->info.hint, item, p, out);
    }
}

void Validator::validateValue(const JValue &v, const TypeRef &t, const PropertyHint &h, int item, const JPath &path,
                              QList<Issue> &out) const
{
    auto expected = [&](const QString &what) {
        add(out, item, path, QStringLiteral("Expected %1, found %2 (%3)").arg(what, v.typeName(), v.preview(30)));
    };
    switch (t.kind) {
    case VKind::Variant:
        return;
    case VKind::Bool:
        if (!v.isBool())
            expected(QStringLiteral("true/false"));
        return;
    case VKind::Int: {
        const EnumInfo *e = enumFor(m_schema, t);
        if (v.isString() && (e || h.kind == PropertyHint::Enum)) {
            const QString n = v.toString();
            if ((e && !e->valueOf(n, nullptr)) || (!e && !optionHasName(h, n)))
                add(out, item, path, QStringLiteral("\"%1\" is not a valid %2 value").arg(n, t.displayName()));
            return;
        }
        if (!v.isNumber()) {
            expected(QStringLiteral("an integer"));
            return;
        }
        if (!v.isIntegral()) {
            add(out, item, path, QStringLiteral("Expected an integer, found %1").arg(fmtNum(v.toDouble())));
            return;
        }
        const qint64 iv = v.toInt();
        if (e && !e->hasValue(iv)) {
            add(out, item, path, QStringLiteral("%1 is not a value of enum %2").arg(iv).arg(e->name));
            return;
        }
        if (h.kind == PropertyHint::Enum && !optionHasValue(h, iv)) {
            add(out, item, path, QStringLiteral("%1 is not one of the allowed options").arg(iv));
            return;
        }
        if (h.kind == PropertyHint::Flags && (iv & ~flagsMask(h)) != 0) {
            add(out, item, path, QStringLiteral("%1 sets flags that do not exist").arg(iv));
            return;
        }
        if (!inRange(double(iv), h))
            add(out, item, path, QStringLiteral("%1 is outside the range %2 – %3").arg(iv).arg(fmtNum(h.min), fmtNum(h.max)));
        return;
    }
    case VKind::Float:
        if (!v.isNumber()) {
            expected(QStringLiteral("a number"));
            return;
        }
        if (!inRange(v.toDouble(), h))
            add(out, item, path, QStringLiteral("%1 is outside the range %2 – %3")
                                     .arg(fmtNum(v.toDouble()), fmtNum(h.min), fmtNum(h.max)));
        return;
    case VKind::String:
    case VKind::StringName:
    case VKind::NodePath: {
        if (!v.isString()) {
            expected(QStringLiteral("a string"));
            return;
        }
        const QString s = v.toString();
        if (h.kind == PropertyHint::Enum && !optionHasName(h, s))
            add(out, item, path, QStringLiteral("\"%1\" is not one of the allowed options").arg(s));
        if (h.kind == PropertyHint::File && !s.isEmpty()) {
            if (!matchesFileFilter(s, h.filters))
                add(out, item, path, QStringLiteral("\"%1\" does not match %2").arg(s, h.filters.join(QStringLiteral(", "))));
            else if (!h.globalPath && !s.startsWith(QLatin1String("res://")) && !s.startsWith(QLatin1String("uid://")))
                add(out, item, path, QStringLiteral("\"%1\" should be a res:// path").arg(s), false);
            else if (s.startsWith(QLatin1String("res://"))
                     && !QFileInfo::exists(m_schema.registry()->toAbs(s)))
                add(out, item, path, QStringLiteral("File \"%1\" does not exist").arg(s), false);
        }
        return;
    }
    case VKind::Color:
        if (!Values::readColor(v, nullptr))
            expected(QStringLiteral("a color (\"#rrggbb\", [r, g, b] or {\"r\", \"g\", \"b\"})"));
        return;
    case VKind::Array: {
        if (!v.isArray()) {
            expected(QStringLiteral("an array"));
            return;
        }
        if (!t.elem)
            return;
        for (int i = 0; i < v.size(); ++i)
            validateValue(v.at(i), *t.elem, h, item, path + PathSeg::i(i), out);
        return;
    }
    case VKind::Dictionary: {
        if (!v.isObject()) {
            expected(QStringLiteral("an object"));
            return;
        }
        if (!t.elem)
            return;
        for (const JMember &m : v.members())
            validateValue(m.value, *t.elem, PropertyHint(), item, path + PathSeg::k(m.key), out);
        return;
    }
    case VKind::Object: {
        const auto mode = m_schema.registry()->objectMode(t.className);
        if (v.isNull())
            return;
        switch (mode) {
        case ClassRegistry::ObjectMode::Inline:
            if (v.isString()) {
                const QString s = v.toString();
                if (!s.startsWith(QLatin1String("res://")) && !s.startsWith(QLatin1String("uid://")))
                    add(out, item, path, QStringLiteral("Expected an inline %1 object or a res:// path").arg(t.displayName()));
                return;
            }
            if (!v.isObject()) {
                expected(QStringLiteral("a %1 object").arg(t.displayName()));
                return;
            }
            validateObject(v, t.className, item, path, out);
            return;
        case ClassRegistry::ObjectMode::ResourcePath:
            if (!v.isString()) {
                expected(QStringLiteral("a res:// path to a %1").arg(t.displayName()));
                return;
            }
            if (v.toString().startsWith(QLatin1String("res://"))
                && !QFileInfo::exists(m_schema.registry()->toAbs(v.toString())))
                add(out, item, path, QStringLiteral("File \"%1\" does not exist").arg(v.toString()), false);
            return;
        case ClassRegistry::ObjectMode::NodeRef:
            if (!v.isString())
                expected(QStringLiteral("a node path"));
            return;
        case ClassRegistry::ObjectMode::Opaque:
            return;
        }
        return;
    }
    default:
        if (t.isVector()) {
            QVector<double> vals;
            if (!Values::readVector(v, t, &vals)) {
                expected(QStringLiteral("a %1 ([%2])").arg(t.displayName(), Values::componentNames(t).join(QStringLiteral(", "))));
                return;
            }
            if (t.isIntVector())
                for (double d : vals)
                    if (std::floor(d) != d) {
                        add(out, item, path, QStringLiteral("%1 components must be integers").arg(t.displayName()));
                        return;
                    }
        }
        return;
    }
}

JValue Validator::sanitizeObject(const JValue &obj, const QString &declaredKey, const QString &keyField) const
{
    if (!obj.isObject())
        return JValue::object();
    const QString cls = m_schema.actualClass(obj, declaredKey);
    Schema::Poly poly;
    const bool hasPoly = m_schema.polyFor(declaredKey, &poly);
    JValue out = JValue::object();
    out.setLayout(obj.layout());
    for (const JMember &m : obj.members()) {
        if (hasPoly && m.key == poly.key) {
            QString err;
            m_schema.actualClass(obj, declaredKey, &err);
            if (err.isEmpty() && m.value.isString())
                out.set(m.key, m.value);
            continue;
        }
        if (!keyField.isEmpty() && m.key == keyField)
            continue;
        const ResolvedProperty *rp = m_schema.registry()->property(cls, m.key);
        if (!rp)
            continue;
        const JValue fixed = sanitizeValue(m.value, rp->type, rp->info.hint);
        if (!fixed.isUndefined())
            out.set(m.key, fixed);
    }
    return out;
}

JValue Validator::sanitizeValue(const JValue &v, const TypeRef &t, const PropertyHint &h) const
{
    QList<Issue> issues;
    validateValue(v, t, h, 0, {}, issues);
    bool hasErrors = false;
    for (const Issue &i : issues)
        hasErrors |= i.error;
    if (!hasErrors)
        return v;

    switch (t.kind) {
    case VKind::Int:
    case VKind::Float: {
        if (!v.isNumber())
            return JValue();
        double c = v.toDouble();
        if (t.kind == VKind::Int)
            c = double(std::llround(c));
        inRange(c, h, &c);
        JValue n = t.kind == VKind::Int ? JValue(qint64(c)) : JValue(c, false);
        QList<Issue> again;
        validateValue(n, t, h, 0, {}, again);
        for (const Issue &i : again)
            if (i.error)
                return JValue();
        return n;
    }
    case VKind::Array: {
        if (!v.isArray() || !t.elem)
            return JValue();
        JValue out = JValue::array();
        out.setLayout(v.layout());
        for (const JValue &e : v.elements()) {
            const JValue f = sanitizeValue(e, *t.elem, h);
            if (!f.isUndefined())
                out.append(f);
        }
        return out;
    }
    case VKind::Dictionary: {
        if (!v.isObject() || !t.elem)
            return JValue();
        JValue out = JValue::object();
        out.setLayout(v.layout());
        for (const JMember &m : v.members()) {
            const JValue f = sanitizeValue(m.value, *t.elem, PropertyHint());
            if (!f.isUndefined())
                out.set(m.key, f);
        }
        return out;
    }
    case VKind::Object:
        if (v.isObject() && m_schema.registry()->objectMode(t.className) == ClassRegistry::ObjectMode::Inline)
            return sanitizeObject(v, t.className);
        return JValue();
    default:
        // Strings with bad options/paths, wrong types: drop so the class default applies.
        return JValue();
    }
}
