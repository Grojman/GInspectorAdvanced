#include "Values.h"
#include "ScriptText.h"

#include <cmath>

using namespace ScriptText;

QString ValueFormat::vectorName(Vector v)
{
    switch (v) {
    case VectorObject: return QStringLiteral("object");
    case VectorString: return QStringLiteral("string");
    default: return QStringLiteral("array");
    }
}

ValueFormat::Vector ValueFormat::vectorFromName(const QString &s)
{
    if (s == QLatin1String("object")) return VectorObject;
    if (s == QLatin1String("string")) return VectorString;
    return VectorArray;
}

QString ValueFormat::colorName(ColorFmt c)
{
    switch (c) {
    case ColorArray: return QStringLiteral("array");
    case ColorObject: return QStringLiteral("object");
    default: return QStringLiteral("hex");
    }
}

ValueFormat::ColorFmt ValueFormat::colorFromName(const QString &s)
{
    if (s == QLatin1String("array")) return ColorArray;
    if (s == QLatin1String("object")) return ColorObject;
    return ColorHex;
}

namespace Values {

QStringList componentNames(const TypeRef &t)
{
    switch (t.kind) {
    case VKind::Vector2: case VKind::Vector2i: return {QStringLiteral("x"), QStringLiteral("y")};
    case VKind::Vector3: case VKind::Vector3i: return {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
    case VKind::Vector4: case VKind::Vector4i:
        return {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"), QStringLiteral("w")};
    case VKind::Rect2: case VKind::Rect2i:
        return {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("w"), QStringLiteral("h")};
    case VKind::Color: return {QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")};
    default: return {};
    }
}

static bool parseTupleString(const QString &s, int n, QVector<double> *out)
{
    QString t = s.trimmed();
    if (t.startsWith(QLatin1Char('(')) && t.endsWith(QLatin1Char(')')))
        t = t.mid(1, t.size() - 2);
    else if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))
        t = t.mid(1, t.size() - 2);
    else
        return false;
    const QStringList parts = t.split(QLatin1Char(','));
    if (parts.size() != n)
        return false;
    QVector<double> vals;
    for (const QString &p : parts) {
        bool ok = false;
        vals << p.trimmed().toDouble(&ok);
        if (!ok)
            return false;
    }
    *out = vals;
    return true;
}

bool readVector(const JValue &v, const TypeRef &t, QVector<double> *out)
{
    const int n = t.components();
    if (n == 0)
        return false;
    QVector<double> vals;
    if (v.isArray()) {
        if (v.size() != n)
            return false;
        for (const JValue &e : v.elements()) {
            if (!e.isNumber())
                return false;
            vals << e.toDouble();
        }
    } else if (v.isObject()) {
        const QStringList names = componentNames(t);
        if (v.size() != n)
            return false;
        for (const QString &k : names) {
            const JValue c = v.value(k);
            if (!c.isNumber())
                return false;
            vals << c.toDouble();
        }
    } else if (v.isString()) {
        if (!parseTupleString(v.toString(), n, &vals))
            return false;
    } else {
        return false;
    }
    if (out)
        *out = vals;
    return true;
}

static JValue num(double d, bool asInt)
{
    if (asInt)
        return JValue(qint64(std::llround(d)));
    return JValue(d, false);
}

static QString numText(double d, bool asInt)
{
    if (asInt)
        return QString::number(qint64(std::llround(d)));
    return QString::number(d, 'g', 15);
}

JValue writeVector(const QVector<double> &vals, const TypeRef &t, const ValueFormat &fmt, const JValue &like)
{
    // Vector values in JSON are read back by Godot as floats; whole numbers stay as
    // written ("14") which is what hand-written files use.
    const bool asInt = t.isIntVector();
    ValueFormat::Vector form = fmt.vector;
    if (like.isArray()) form = ValueFormat::VectorArray;
    else if (like.isObject()) form = ValueFormat::VectorObject;
    else if (like.isString()) form = ValueFormat::VectorString;

    const QStringList names = componentNames(t);
    switch (form) {
    case ValueFormat::VectorObject: {
        JValue o = JValue::object();
        o.setLayout(like.isObject() ? like.layout() : JValue::LayoutInline);
        for (int i = 0; i < names.size() && i < vals.size(); ++i)
            o.set(names[i], num(vals[i], asInt || vals[i] == std::floor(vals[i])));
        return o;
    }
    case ValueFormat::VectorString: {
        QStringList parts;
        for (double d : vals)
            parts << numText(d, asInt);
        return JValue(QStringLiteral("(%1)").arg(parts.join(QStringLiteral(", "))));
    }
    default: {
        JValue a = JValue::array();
        a.setLayout(like.isArray() ? like.layout() : JValue::LayoutInline);
        for (double d : vals)
            a.append(num(d, asInt || d == std::floor(d)));
        return a;
    }
    }
}

bool readColor(const JValue &v, QColor *out)
{
    QColor c;
    if (v.isString()) {
        QString s = v.toString().trimmed();
        if (!s.startsWith(QLatin1Char('#')))
            s.prepend(QLatin1Char('#'));
        const QString hex = s.mid(1);
        bool ok = false;
        hex.toUInt(&ok, 16);
        if (!ok)
            return false;
        auto ch = [&](int i, int len) { return hex.mid(i, len).toInt(nullptr, 16); };
        if (hex.size() == 3 || hex.size() == 4) {
            c.setRgb(ch(0, 1) * 17, ch(1, 1) * 17, ch(2, 1) * 17, hex.size() == 4 ? ch(3, 1) * 17 : 255);
        } else if (hex.size() == 6 || hex.size() == 8) {
            // Godot's Color(html) reads #RRGGBBAA (alpha last).
            c.setRgb(ch(0, 2), ch(2, 2), ch(4, 2), hex.size() == 8 ? ch(6, 2) : 255);
        } else {
            return false;
        }
    } else if (v.isArray()) {
        if (v.size() != 3 && v.size() != 4)
            return false;
        double f[4] = {0, 0, 0, 1};
        for (int i = 0; i < v.size(); ++i) {
            if (!v.at(i).isNumber())
                return false;
            f[i] = v.at(i).toDouble();
        }
        c.setRgbF(float(qBound(0.0, f[0], 1.0)), float(qBound(0.0, f[1], 1.0)), float(qBound(0.0, f[2], 1.0)),
                  float(qBound(0.0, f[3], 1.0)));
    } else if (v.isObject()) {
        double f[4] = {0, 0, 0, 1};
        const char *keys[4] = {"r", "g", "b", "a"};
        for (int i = 0; i < 4; ++i) {
            const JValue x = v.value(QLatin1String(keys[i]));
            if (x.isUndefined() && i == 3)
                continue;
            if (!x.isNumber())
                return false;
            f[i] = x.toDouble();
        }
        c.setRgbF(float(qBound(0.0, f[0], 1.0)), float(qBound(0.0, f[1], 1.0)), float(qBound(0.0, f[2], 1.0)),
                  float(qBound(0.0, f[3], 1.0)));
    } else {
        return false;
    }
    if (out)
        *out = c;
    return true;
}

static double round4(double d)
{
    return std::round(d * 10000.0) / 10000.0;
}

JValue writeColor(const QColor &c, const ValueFormat &fmt, bool alpha, const JValue &like)
{
    ValueFormat::ColorFmt form = fmt.color;
    if (like.isString()) form = ValueFormat::ColorHex;
    else if (like.isArray()) form = ValueFormat::ColorArray;
    else if (like.isObject()) form = ValueFormat::ColorObject;
    const bool writeAlpha = alpha && (c.alpha() != 255 || (like.isArray() && like.size() == 4)
                                      || (like.isObject() && like.contains(QStringLiteral("a")))
                                      || (like.isString() && like.toString().remove(QLatin1Char('#')).size() == 8));
    switch (form) {
    case ValueFormat::ColorArray: {
        JValue a = JValue::array();
        a.setLayout(like.isArray() ? like.layout() : JValue::LayoutInline);
        a.append(JValue(round4(c.redF())));
        a.append(JValue(round4(c.greenF())));
        a.append(JValue(round4(c.blueF())));
        if (writeAlpha)
            a.append(JValue(round4(c.alphaF())));
        return a;
    }
    case ValueFormat::ColorObject: {
        JValue o = JValue::object();
        o.setLayout(like.isObject() ? like.layout() : JValue::LayoutInline);
        o.set(QStringLiteral("r"), JValue(round4(c.redF())));
        o.set(QStringLiteral("g"), JValue(round4(c.greenF())));
        o.set(QStringLiteral("b"), JValue(round4(c.blueF())));
        if (writeAlpha)
            o.set(QStringLiteral("a"), JValue(round4(c.alphaF())));
        return o;
    }
    default: {
        QString s = QStringLiteral("#%1%2%3")
                        .arg(c.red(), 2, 16, QLatin1Char('0'))
                        .arg(c.green(), 2, 16, QLatin1Char('0'))
                        .arg(c.blue(), 2, 16, QLatin1Char('0'));
        if (writeAlpha)
            s += QStringLiteral("%1").arg(c.alpha(), 2, 16, QLatin1Char('0'));
        if (like.isString() && !like.toString().startsWith(QLatin1Char('#')))
            s = s.mid(1);
        return JValue(s);
    }
    }
}

JValue typeDefault(const TypeRef &t, const ValueFormat &fmt)
{
    switch (t.kind) {
    case VKind::Variant: return JValue::null();
    case VKind::Bool: return JValue(false);
    case VKind::Int: return JValue(0);
    case VKind::Float: return JValue(0.0, false);
    case VKind::String: case VKind::StringName: case VKind::NodePath: return JValue(QString());
    case VKind::Color: return writeColor(QColor(0, 0, 0, 255), fmt, true);
    case VKind::Array: return JValue::array();
    case VKind::Dictionary: return JValue::object();
    case VKind::Object: return JValue::null();
    default:
        if (t.isVector())
            return writeVector(QVector<double>(t.components(), 0.0), t, fmt);
        return JValue::null();
    }
}

namespace {

TypeRef simple(VKind k)
{
    TypeRef t;
    t.kind = k;
    return t;
}

bool parseNumberLiteral(QString s, double *out, bool *isInt)
{
    s = s.trimmed();
    s.remove(QLatin1Char('_'));
    bool neg = false;
    if (s.startsWith(QLatin1Char('-'))) {
        neg = true;
        s = s.mid(1).trimmed();
    } else if (s.startsWith(QLatin1Char('+'))) {
        s = s.mid(1).trimmed();
    }
    bool ok = false;
    double d = 0;
    bool integer = true;
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        d = double(s.mid(2).toLongLong(&ok, 16));
    } else if (s.startsWith(QLatin1String("0b"), Qt::CaseInsensitive)) {
        d = double(s.mid(2).toLongLong(&ok, 2));
    } else {
        // C# suffixes
        if (s.endsWith(QLatin1Char('f'), Qt::CaseInsensitive) || s.endsWith(QLatin1Char('d'), Qt::CaseInsensitive)
            || s.endsWith(QLatin1Char('m'), Qt::CaseInsensitive)) {
            s.chop(1);
            integer = false;
        } else if (s.endsWith(QLatin1Char('L'), Qt::CaseInsensitive)) {
            s.chop(1);
        }
        if (s.contains(QLatin1Char('.')) || s.contains(QLatin1Char('e')) || s.contains(QLatin1Char('E')))
            integer = false;
        if (s == QLatin1String("PI")) { d = 3.14159265358979323846; ok = true; integer = false; }
        else if (s == QLatin1String("TAU")) { d = 6.28318530717958647692; ok = true; integer = false; }
        else if (s == QLatin1String("INF")) { d = 1e300; ok = true; integer = false; }
        else if (!s.isEmpty() && (s[0].isDigit() || s[0] == QLatin1Char('.')))
            d = s.toDouble(&ok);
    }
    if (!ok)
        return false;
    *out = neg ? -d : d;
    *isInt = integer;
    return true;
}

const QHash<QString, QColor> &namedColors()
{
    static const QHash<QString, QColor> m = {
        {QStringLiteral("WHITE"), QColor(255, 255, 255)}, {QStringLiteral("BLACK"), QColor(0, 0, 0)},
        {QStringLiteral("RED"), QColor(255, 0, 0)}, {QStringLiteral("GREEN"), QColor(0, 255, 0)},
        {QStringLiteral("BLUE"), QColor(0, 0, 255)}, {QStringLiteral("YELLOW"), QColor(255, 255, 0)},
        {QStringLiteral("CYAN"), QColor(0, 255, 255)}, {QStringLiteral("MAGENTA"), QColor(255, 0, 255)},
        {QStringLiteral("GRAY"), QColor(190, 190, 190)}, {QStringLiteral("ORANGE"), QColor(255, 165, 0)},
        {QStringLiteral("PURPLE"), QColor(160, 32, 240)}, {QStringLiteral("TRANSPARENT"), QColor(255, 255, 255, 0)},
        {QStringLiteral("PINK"), QColor(255, 192, 203)}, {QStringLiteral("BROWN"), QColor(165, 42, 42)},
    };
    return m;
}

VKind vectorKind(const QString &name)
{
    static const QHash<QString, VKind> m = {
        {QStringLiteral("Vector2"), VKind::Vector2}, {QStringLiteral("Vector2i"), VKind::Vector2i},
        {QStringLiteral("Vector3"), VKind::Vector3}, {QStringLiteral("Vector3i"), VKind::Vector3i},
        {QStringLiteral("Vector4"), VKind::Vector4}, {QStringLiteral("Vector4i"), VKind::Vector4i},
        {QStringLiteral("Rect2"), VKind::Rect2}, {QStringLiteral("Rect2i"), VKind::Rect2i},
    };
    return m.value(name, VKind::Variant);
}

} // namespace

JValue evalExpr(const QString &exprIn, const TypeRef &t, EvalContext &ctx, TypeRef *guess)
{
    if (ctx.depth > 16)
        return JValue();
    QString e = exprIn.trimmed();
    if (e.isEmpty())
        return JValue();
    if (e.startsWith(QLatin1String("new ")))
        e = e.mid(4).trimmed();
    // Strip a trailing cast: "x as int"
    const int asPos = findTopLevelToken(e, QStringLiteral(" as "));
    if (asPos > 0)
        e = e.left(asPos).trimmed();
    // Outer parentheses
    if (e.startsWith(QLatin1Char('(')) && matchBracket(e, 0) == e.size())
        return evalExpr(e.mid(1, e.size() - 2), t, ctx, guess);

    auto setGuess = [&](const TypeRef &g) { if (guess) *guess = g; };

    // Numbers
    double d = 0;
    bool isInt = false;
    if (parseNumberLiteral(e, &d, &isInt)) {
        setGuess(simple(isInt ? VKind::Int : VKind::Float));
        if (t.kind == VKind::Float)
            return JValue(d, false);
        if (t.kind == VKind::Int)
            return JValue(qint64(std::llround(d)));
        return JValue(d, isInt);
    }
    // Strings
    QString str;
    if (unquote(e, &str)) {
        if (e.startsWith(QLatin1Char('&'))) setGuess(simple(VKind::StringName));
        else if (e.startsWith(QLatin1Char('^'))) setGuess(simple(VKind::NodePath));
        else setGuess(simple(VKind::String));
        if (t.kind == VKind::Color) {
            QColor c;
            JValue sv(str);
            if (readColor(sv, &c))
                return writeColor(c, ctx.fmt, true);
        }
        return JValue(str);
    }
    if (e == QLatin1String("true") || e == QLatin1String("false")) {
        setGuess(simple(VKind::Bool));
        return JValue(e == QLatin1String("true"));
    }
    if (e == QLatin1String("null")) {
        return JValue::null();
    }

    // Arrays
    if (e.startsWith(QLatin1Char('[')) && matchBracket(e, 0) == e.size()) {
        JValue arr = JValue::array();
        const TypeRef elemType = t.kind == VKind::Array && t.elem ? *t.elem : TypeRef();
        for (const QString &part : splitTopLevel(e.mid(1, e.size() - 2))) {
            if (part.isEmpty())
                continue;
            ctx.depth++;
            JValue v = evalExpr(part, elemType, ctx);
            ctx.depth--;
            if (v.isUndefined())
                return JValue();
            arr.append(v);
        }
        setGuess(simple(VKind::Array));
        return arr;
    }
    // Dictionaries
    if (e.startsWith(QLatin1Char('{')) && matchBracket(e, 0) == e.size()) {
        JValue obj = JValue::object();
        const TypeRef valType = t.kind == VKind::Dictionary && t.elem ? *t.elem : TypeRef();
        for (const QString &part : splitTopLevel(e.mid(1, e.size() - 2))) {
            if (part.isEmpty())
                continue;
            int sep = findTopLevel(part, QStringLiteral(":="));
            if (sep < 0)
                return JValue();
            QString k = part.left(sep).trimmed();
            QString key;
            if (!unquote(k, &key)) {
                if (part[sep] == QLatin1Char('='))
                    key = k; // Lua style: { key = value }
                else {
                    TypeRef dummy;
                    ctx.depth++;
                    JValue kv = evalExpr(k, TypeRef(), ctx);
                    ctx.depth--;
                    if (kv.isString()) key = kv.toString();
                    else if (kv.isNumber()) key = QString::number(kv.toDouble(), 'g', 15);
                    else return JValue();
                }
            }
            ctx.depth++;
            JValue v = evalExpr(part.mid(sep + 1), valType, ctx);
            ctx.depth--;
            if (v.isUndefined())
                return JValue();
            obj.set(key, v);
        }
        setGuess(simple(VKind::Dictionary));
        return obj;
    }

    // Constructors: Vector2(1, 2), Color(1, 0, 0), Color("#fff"), Array[int]([]), ...
    const int paren = e.indexOf(QLatin1Char('('));
    if (paren > 0 && e.endsWith(QLatin1Char(')')) && matchBracket(e, paren) == e.size()) {
        const QString callee = e.left(paren).trimmed();
        const QStringList args = splitTopLevel(e.mid(paren + 1, e.size() - paren - 2));
        const VKind vk = vectorKind(callee);
        if (vk != VKind::Variant) {
            TypeRef vt = simple(vk);
            setGuess(vt);
            QVector<double> vals;
            for (const QString &a : args) {
                if (a.isEmpty())
                    continue;
                ctx.depth++;
                JValue v = evalExpr(a, simple(VKind::Float), ctx);
                ctx.depth--;
                if (v.isNumber()) {
                    vals << v.toDouble();
                } else {
                    QVector<double> sub;
                    TypeRef st = simple(VKind::Vector2);
                    if (readVector(v, st, &sub)) vals << sub;
                    else return JValue();
                }
            }
            if (vals.isEmpty())
                vals = QVector<double>(vt.components(), 0.0);
            if (vals.size() == 1)
                vals = QVector<double>(vt.components(), vals[0]);
            if (vals.size() != vt.components())
                return JValue();
            return writeVector(vals, vt, ctx.fmt);
        }
        if (callee == QLatin1String("Color") || callee == QLatin1String("Color8")) {
            setGuess(simple(VKind::Color));
            if (args.size() == 1 || (args.size() == 2 && unquote(args[0], nullptr))) {
                QString s;
                if (unquote(args[0], &s)) {
                    QColor c;
                    if (readColor(JValue(s), &c)) {
                        if (args.size() == 2)
                            c.setAlphaF(float(args[1].toDouble()));
                        return writeColor(c, ctx.fmt, true);
                    }
                }
                return JValue();
            }
            if (args.size() < 3)
                return writeColor(QColor(0, 0, 0), ctx.fmt, true);
            double f[4] = {0, 0, 0, 1};
            for (int i = 0; i < args.size() && i < 4; ++i) {
                ctx.depth++;
                JValue v = evalExpr(args[i], simple(VKind::Float), ctx);
                ctx.depth--;
                if (!v.isNumber())
                    return JValue();
                f[i] = v.toDouble();
            }
            if (callee == QLatin1String("Color8"))
                for (double &x : f)
                    x /= 255.0;
            QColor c;
            c.setRgbF(float(qBound(0.0, f[0], 1.0)), float(qBound(0.0, f[1], 1.0)), float(qBound(0.0, f[2], 1.0)),
                      float(qBound(0.0, f[3], 1.0)));
            return writeColor(c, ctx.fmt, true);
        }
        if (callee.endsWith(QLatin1String(".new"))) {
            TypeRef g;
            g.kind = VKind::Object;
            g.raw = callee.left(callee.size() - 4);
            setGuess(g);
            return JValue::object();
        }
        if (callee.startsWith(QLatin1String("Array")) && args.size() == 1) {
            ctx.depth++;
            JValue v = evalExpr(args[0], t, ctx);
            ctx.depth--;
            setGuess(simple(VKind::Array));
            return v;
        }
        if (callee == QLatin1String("StringName") || callee == QLatin1String("NodePath") || callee == QLatin1String("String")) {
            ctx.depth++;
            JValue v = evalExpr(args.value(0), simple(VKind::String), ctx);
            ctx.depth--;
            setGuess(simple(callee == QLatin1String("String") ? VKind::String
                            : callee == QLatin1String("NodePath") ? VKind::NodePath : VKind::StringName));
            return v.isUndefined() && args.isEmpty() ? JValue(QString()) : v;
        }
        if (callee == QLatin1String("Dictionary") && args.isEmpty()) {
            setGuess(simple(VKind::Dictionary));
            return JValue::object();
        }
        // C#: new Godot.Collections.Array<T>() / new List<T>() etc.
        if (args.isEmpty() && (callee.startsWith(QLatin1String("Array")) || callee.startsWith(QLatin1String("List"))
                               || callee.contains(QLatin1String("Collections.Array")))) {
            setGuess(simple(VKind::Array));
            return JValue::array();
        }
        if (args.isEmpty() && callee.contains(QLatin1String("Dictionary"))) {
            setGuess(simple(VKind::Dictionary));
            return JValue::object();
        }
        if (args.isEmpty() && !callee.contains(QLatin1Char('.'))) {
            // C#: new MyResource()
            TypeRef g;
            g.kind = VKind::Object;
            g.raw = callee;
            setGuess(g);
            return JValue::object();
        }
        return JValue();
    }

    // Dotted constants: Vector2.ONE, Color.RED, Colors.Red, Enum.MEMBER, Class.CONST
    const int dot = e.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        const QString head = e.left(dot).trimmed();
        const QString member = e.mid(dot + 1).trimmed();
        const VKind vk = vectorKind(head);
        if (vk != VKind::Variant) {
            TypeRef vt = simple(vk);
            setGuess(vt);
            const int n = vt.components();
            QVector<double> vals(n, 0.0);
            const QString m = member.toUpper();
            if (m == QLatin1String("ONE")) vals.fill(1.0);
            else if (m == QLatin1String("INF")) vals.fill(1e300);
            else if (m == QLatin1String("UP")) vals[1] = -1;
            else if (m == QLatin1String("DOWN")) vals[1] = 1;
            else if (m == QLatin1String("LEFT")) vals[0] = -1;
            else if (m == QLatin1String("RIGHT")) vals[0] = 1;
            else if (m == QLatin1String("FORWARD") && n >= 3) vals[2] = -1;
            else if (m == QLatin1String("BACK") && n >= 3) vals[2] = 1;
            return writeVector(vals, vt, ctx.fmt);
        }
        if (head == QLatin1String("Color") || head == QLatin1String("Colors")) {
            setGuess(simple(VKind::Color));
            const QColor c = namedColors().value(member.toUpper(), QColor());
            if (c.isValid())
                return writeColor(c, ctx.fmt, true);
            return JValue();
        }
    }

    // Identifiers resolved by the caller (consts, enum members).
    if (ctx.lookup) {
        const QString repl = ctx.lookup(e);
        if (!repl.isEmpty() && repl != e) {
            ctx.depth++;
            JValue v = evalExpr(repl, t, ctx, guess);
            ctx.depth--;
            return v;
        }
    }
    return JValue();
}

} // namespace Values
