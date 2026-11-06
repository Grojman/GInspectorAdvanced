#include "CSharpParser.h"
#include "ScriptText.h"

#include <QFile>
#include <QRegularExpression>

using namespace ScriptText;

namespace {

const QChar kDocOpen(0x01);
const QChar kDocClose(0x02);

// Removes comments; "///" doc comments become kDocOpen text kDocClose markers.
QString stripComments(const QString &src)
{
    QString out;
    out.reserve(src.size());
    int i = 0;
    const int n = src.size();
    while (i < n) {
        const QChar c = src[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            // verbatim strings (@"..") contain no escapes but keep it simple
            const int start = i;
            const QChar q = c;
            ++i;
            while (i < n && src[i] != q) {
                if (src[i] == QLatin1Char('\\'))
                    ++i;
                ++i;
            }
            ++i;
            out += src.mid(start, i - start);
            continue;
        }
        if (src.mid(i, 3) == QLatin1String("///")) {
            int e = src.indexOf(QLatin1Char('\n'), i);
            if (e < 0)
                e = n;
            QString doc = src.mid(i + 3, e - i - 3).trimmed();
            doc.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            if (!doc.trimmed().isEmpty())
                out += kDocOpen + doc.trimmed() + kDocClose;
            i = e;
            continue;
        }
        if (src.mid(i, 2) == QLatin1String("//")) {
            int e = src.indexOf(QLatin1Char('\n'), i);
            i = e < 0 ? n : e;
            continue;
        }
        if (src.mid(i, 2) == QLatin1String("/*")) {
            int e = src.indexOf(QLatin1String("*/"), i + 2);
            i = e < 0 ? n : e + 2;
            out += QLatin1Char(' ');
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

QString takeDocs(QString &header)
{
    QStringList docs;
    int open;
    while ((open = header.indexOf(kDocOpen)) >= 0) {
        const int close = header.indexOf(kDocClose, open);
        if (close < 0)
            break;
        docs << header.mid(open + 1, close - open - 1);
        header.remove(open, close - open + 1);
    }
    return docs.join(QLatin1Char('\n'));
}

struct Attribute
{
    QString name;
    QStringList args;
};

QList<Attribute> takeAttributes(QString &header)
{
    QList<Attribute> out;
    while (true) {
        const QString t = header.trimmed();
        if (!t.startsWith(QLatin1Char('[')))
            break;
        const int end = matchBracket(t, 0);
        if (end < 0)
            break;
        const QString body = t.mid(1, end - 2);
        for (const QString &one : splitTopLevel(body)) {
            Attribute a;
            int p = 0;
            QString name;
            while (p < one.size() && (isIdentChar(one[p]) || one[p] == QLatin1Char('.')))
                name += one[p++];
            a.name = name.section(QLatin1Char('.'), -1);
            if (a.name.endsWith(QLatin1String("Attribute")))
                a.name.chop(9);
            const int paren = one.indexOf(QLatin1Char('('));
            if (paren >= 0) {
                const int pe = matchBracket(one, paren);
                a.args = splitTopLevel(one.mid(paren + 1, (pe < 0 ? one.size() : pe - 1) - paren - 1));
            }
            out << a;
        }
        header = t.mid(end);
    }
    return out;
}

QString argStr(const Attribute &a, int i)
{
    if (i >= a.args.size())
        return QString();
    QString s;
    if (unquote(a.args[i], &s))
        return s;
    return a.args[i].trimmed();
}

void applyExport(const Attribute &a, PropertyInfo &p)
{
    p.exported = true;
    if (a.args.isEmpty())
        return;
    const QString hint = argStr(a, 0).section(QLatin1Char('.'), -1);
    const QString hs = argStr(a, 1);
    PropertyHint &h = p.hint;
    if (hint == QLatin1String("Range")) {
        h.kind = PropertyHint::Range;
        const QStringList parts = hs.split(QLatin1Char(','));
        bool ok = false;
        if (parts.size() > 0) { h.min = parts[0].trimmed().toDouble(&ok); h.hasMin = ok; }
        if (parts.size() > 1) { h.max = parts[1].trimmed().toDouble(&ok); h.hasMax = ok; }
        int extra = 2;
        if (parts.size() > 2) {
            const double st = parts[2].trimmed().toDouble(&ok);
            if (ok) { h.step = st; extra = 3; }
        }
        for (int i = extra; i < parts.size(); ++i) {
            const QString e = parts[i].trimmed();
            if (e == QLatin1String("or_greater")) h.orGreater = true;
            else if (e == QLatin1String("or_less")) h.orLess = true;
            else if (e == QLatin1String("hide_slider")) h.hideSlider = true;
            else if (e.startsWith(QLatin1String("suffix:"))) h.suffix = e.mid(7);
        }
    } else if (hint == QLatin1String("Enum") || hint == QLatin1String("Flags")) {
        const bool flags = hint == QLatin1String("Flags");
        h.kind = flags ? PropertyHint::Flags : PropertyHint::Enum;
        qint64 next = flags ? 1 : 0;
        for (QString e : hs.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            e = e.trimmed();
            qint64 v = next;
            const int colon = e.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                bool ok = false;
                const qint64 x = e.mid(colon + 1).toLongLong(&ok);
                if (ok) { v = x; e = e.left(colon); }
            }
            h.options << qMakePair(e, v);
            next = flags ? v << 1 : v + 1;
        }
    } else if (hint == QLatin1String("File") || hint == QLatin1String("GlobalFile")) {
        h.kind = PropertyHint::File;
        h.globalPath = hint == QLatin1String("GlobalFile");
        for (const QString &f : hs.split(QLatin1Char(','), Qt::SkipEmptyParts))
            h.filters << f.trimmed().section(QLatin1Char(';'), 0, 0).trimmed();
    } else if (hint == QLatin1String("Dir") || hint == QLatin1String("GlobalDir")) {
        h.kind = PropertyHint::Dir;
    } else if (hint == QLatin1String("MultilineText")) {
        h.kind = PropertyHint::Multiline;
    } else if (hint == QLatin1String("PlaceholderText")) {
        h.kind = PropertyHint::Placeholder;
        h.placeholder = hs;
    } else if (hint == QLatin1String("ColorNoAlpha")) {
        h.kind = PropertyHint::ColorNoAlpha;
    } else if (hint == QLatin1String("ExpEasing")) {
        h.kind = PropertyHint::ExpEasing;
    } else if (hint.startsWith(QLatin1String("Layers"))) {
        h.kind = PropertyHint::Flags;
        for (int i = 0; i < 32; ++i)
            h.options << qMakePair(QStringLiteral("Layer %1").arg(i + 1), qint64(1) << i);
    }
}

const QStringList &modifiers()
{
    static const QStringList m = {
        QStringLiteral("public"), QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("internal"),
        QStringLiteral("static"), QStringLiteral("readonly"), QStringLiteral("const"), QStringLiteral("virtual"),
        QStringLiteral("override"), QStringLiteral("new"), QStringLiteral("partial"), QStringLiteral("required"),
        QStringLiteral("abstract"), QStringLiteral("sealed"), QStringLiteral("unsafe"), QStringLiteral("volatile"),
        QStringLiteral("extern"), QStringLiteral("event"), QStringLiteral("file")};
    return m;
}

// Splits leading modifiers off a declaration header.
QStringList takeModifiers(QString &header)
{
    QStringList found;
    while (true) {
        header = header.trimmed();
        int p = 0;
        const QString w = readIdent(header, p);
        if (w.isEmpty() || !modifiers().contains(w))
            break;
        found << w;
        header = header.mid(p);
    }
    return found;
}

struct Context
{
    QString resPath;
    CSharpParser::Options opts;
    QList<ClassPtr> classes;
};

void parseEnum(const QString &name, const QString &body, ClassInfo &cls)
{
    EnumInfo info;
    info.name = name;
    qint64 next = 0;
    for (QString e : splitTopLevel(body)) {
        takeDocs(e);
        takeAttributes(e);
        e = e.trimmed();
        if (e.isEmpty())
            continue;
        qint64 v = next;
        const int eq = e.indexOf(QLatin1Char('='));
        if (eq > 0) {
            bool ok = false;
            const qint64 x = e.mid(eq + 1).trimmed().toLongLong(&ok);
            if (ok)
                v = x;
            e = e.left(eq).trimmed();
        }
        info.values << qMakePair(e, v);
        next = v + 1;
    }
    cls.enums.insert(name, info);
}

void addMember(QString header, const QString &defaultExpr, ClassInfo &cls, QString &group, QString &subgroup,
               const Context &ctx)
{
    const QString doc = takeDocs(header);
    const QList<Attribute> attrs = takeAttributes(header);
    const QStringList mods = takeModifiers(header);
    header = header.trimmed();

    bool exported = false;
    for (const Attribute &a : attrs) {
        if (a.name == QLatin1String("ExportGroup")) { group = argStr(a, 0); subgroup.clear(); }
        else if (a.name == QLatin1String("ExportSubgroup")) subgroup = argStr(a, 0);
        else if (a.name == QLatin1String("ExportCategory")) { group = argStr(a, 0); subgroup.clear(); }
        else if (a.name == QLatin1String("Export")) exported = true;
    }
    if (mods.contains(QLatin1String("static")) || mods.contains(QLatin1String("event")))
        return;
    if (mods.contains(QLatin1String("const"))) {
        const QString name = header.section(QLatin1Char(' '), -1).trimmed();
        if (!name.isEmpty())
            cls.consts.insert(name, defaultExpr);
        return;
    }
    const bool isPublic = mods.contains(QLatin1String("public"));
    if (!exported) {
        if (ctx.opts.scope == VarScope::Exported)
            return;
        if (ctx.opts.scope == VarScope::Public && !isPublic)
            return;
    }

    // header is now "Type Name" (type may contain generics and spaces after commas)
    int nameEnd = header.size();
    while (nameEnd > 0 && header[nameEnd - 1].isSpace())
        --nameEnd;
    int nameStart = nameEnd;
    while (nameStart > 0 && isIdentChar(header[nameStart - 1]))
        --nameStart;
    const QString name = header.mid(nameStart, nameEnd - nameStart);
    const QString type = header.left(nameStart).trimmed();
    if (name.isEmpty() || type.isEmpty())
        return;

    PropertyInfo p;
    p.name = name;
    p.typeRaw = CSharpParser::translateType(type);
    p.defaultExpr = defaultExpr.trimmed();
    p.doc = doc;
    p.group = group;
    p.subgroup = subgroup;
    for (const Attribute &a : attrs)
        if (a.name == QLatin1String("Export"))
            applyExport(a, p);
    p.exported = exported;
    cls.properties << p;
}

void parseBody(const QString &body, ClassInfo *cls, const QString &outerKey, Context &ctx);

void parseTypeDecl(const QString &header, const QString &body, const QString &outerKey, Context &ctx)
{
    static const QRegularExpression re(QStringLiteral("\\b(class|struct|record)\\s+([A-Za-z_]\\w*)(\\s*<[^>]*>)?\\s*(:\\s*([^\\{]*))?"));
    const QRegularExpressionMatch m = re.match(header);
    if (!m.hasMatch())
        return;
    auto cls = std::make_shared<ClassInfo>();
    cls->language = QStringLiteral("csharp");
    cls->scriptPath = ctx.resPath;
    cls->name = m.captured(2);
    QString h = header;
    cls->doc = takeDocs(h);
    if (outerKey.isEmpty()) {
        cls->key = cls->name;
        cls->global = true;
    } else {
        cls->key = outerKey + QLatin1Char('.') + cls->name;
        cls->outerKey = outerKey;
    }
    const QString bases = m.captured(5).trimmed();
    if (!bases.isEmpty()) {
        const QString first = splitTopLevel(bases).value(0).trimmed();
        cls->baseRaw = CSharpParser::translateType(first);
    } else {
        cls->baseRaw = QStringLiteral("RefCounted");
    }
    ctx.classes << cls;
    parseBody(body, cls.get(), cls->key, ctx);
    if (!outerKey.isEmpty()) {
        for (const ClassPtr &c : ctx.classes)
            if (c->key == outerKey)
                c->innerKeys << cls->key;
    }
}

void parseBody(const QString &body, ClassInfo *cls, const QString &outerKey, Context &ctx)
{
    QString group, subgroup;
    int i = 0;
    int start = 0;
    const int n = body.size();
    while (i < n) {
        const QChar c = body[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar q = c;
            ++i;
            while (i < n && body[i] != q) {
                if (body[i] == QLatin1Char('\\'))
                    ++i;
                ++i;
            }
            ++i;
            continue;
        }
        if (c == QLatin1Char('[')) {
            // attribute list: skip as a unit so its commas/brackets don't confuse us
            const int e = matchBracket(body, i);
            i = e < 0 ? n : e;
            continue;
        }
        if (c == QLatin1Char('(')) {
            const int e = matchBracket(body, i);
            i = e < 0 ? n : e;
            continue;
        }
        if (c == QLatin1Char(';')) {
            QString stmt = body.mid(start, i - start);
            start = ++i;
            if (!cls)
                continue;
            QString probe = stmt;
            takeDocs(probe);
            takeAttributes(probe);
            if (probe.contains(QLatin1String("=>")) || probe.trimmed().startsWith(QLatin1String("using")))
                continue; // expression-bodied member or using directive
            const int eq = findTopLevel(stmt, QStringLiteral("="));
            const QString header = eq >= 0 ? stmt.left(eq) : stmt;
            const QString def = eq >= 0 ? stmt.mid(eq + 1) : QString();
            QString bare = header;
            takeDocs(bare);
            takeAttributes(bare);
            if (bare.contains(QLatin1Char('(')))
                continue; // method declaration
            // "int a, b;" declares several fields
            const QStringList parts = splitTopLevel(header);
            if (parts.size() > 1) {
                const QString first = parts.first();
                const int sp = first.lastIndexOf(QLatin1Char(' '));
                const QString prefix = sp >= 0 ? first.left(sp) : QString();
                addMember(first, QString(), *cls, group, subgroup, ctx);
                for (int k = 1; k < parts.size(); ++k)
                    addMember(prefix + QLatin1Char(' ') + parts[k], QString(), *cls, group, subgroup, ctx);
            } else {
                addMember(header, def, *cls, group, subgroup, ctx);
            }
            continue;
        }
        if (c == QLatin1Char('{')) {
            const int e = matchBracket(body, i);
            const int end = e < 0 ? n : e;
            const QString header = body.mid(start, i - start);
            const QString inner = body.mid(i + 1, end - i - 2);
            QString probe = header;
            takeDocs(probe);
            takeAttributes(probe);
            static const QRegularExpression typeRe(QStringLiteral("\\b(class|struct|record|interface)\\s+[A-Za-z_]"));
            static const QRegularExpression enumRe(QStringLiteral("\\benum\\s+([A-Za-z_]\\w*)"));
            i = end;
            start = end;
            if (probe.contains(QLatin1String("namespace"))) {
                parseBody(inner, nullptr, outerKey, ctx);
            } else if (typeRe.match(probe).hasMatch()) {
                if (!probe.contains(QLatin1String("interface")))
                    parseTypeDecl(header, inner, cls ? cls->key : QString(), ctx);
            } else if (const auto em = enumRe.match(probe); em.hasMatch()) {
                if (cls)
                    parseEnum(em.captured(1), inner, *cls);
            } else if (probe.contains(QLatin1Char('('))) {
                // method / constructor body: skip
            } else if (cls && inner.contains(QLatin1String("get"))) {
                // auto-property, possibly followed by "= value;"
                QString def;
                int k = i;
                while (k < n && body[k].isSpace())
                    ++k;
                if (k < n && body[k] == QLatin1Char('=')) {
                    const int semi = findTopLevel(body, QStringLiteral(";"), k + 1);
                    const int stop = semi < 0 ? n : semi;
                    def = body.mid(k + 1, stop - k - 1);
                    i = stop + 1;
                    start = i;
                }
                if (!inner.contains(QLatin1String("set")) && !inner.contains(QLatin1String("init")))
                    continue; // read-only property
                addMember(header, def, *cls, group, subgroup, ctx);
            }
            continue;
        }
        ++i;
    }
}

} // namespace

QString CSharpParser::translateType(const QString &csTypeIn)
{
    QString t = csTypeIn.trimmed();
    t.remove(QStringLiteral("global::"));
    if (t.endsWith(QLatin1Char('?')))
        t.chop(1);
    t.replace(QStringLiteral("Godot.Collections."), QString());
    t.replace(QStringLiteral("System.Collections.Generic."), QString());
    t.replace(QStringLiteral("Godot."), QString());
    if (t.endsWith(QLatin1String("[]")))
        return QStringLiteral("Array[%1]").arg(translateType(t.left(t.size() - 2)));
    const int lt = t.indexOf(QLatin1Char('<'));
    if (lt > 0 && t.endsWith(QLatin1Char('>'))) {
        const QString outer = t.left(lt).trimmed();
        const QStringList args = splitTopLevel(t.mid(lt + 1, t.size() - lt - 2));
        if (outer == QLatin1String("Array") || outer == QLatin1String("List"))
            return QStringLiteral("Array[%1]").arg(translateType(args.value(0)));
        if (outer == QLatin1String("Dictionary") && args.size() == 2)
            return QStringLiteral("Dictionary[%1, %2]").arg(translateType(args[0]), translateType(args[1]));
        return outer;
    }
    static const QHash<QString, QString> map = {
        {QStringLiteral("int"), QStringLiteral("int")}, {QStringLiteral("long"), QStringLiteral("int")},
        {QStringLiteral("short"), QStringLiteral("int")}, {QStringLiteral("byte"), QStringLiteral("int")},
        {QStringLiteral("sbyte"), QStringLiteral("int")}, {QStringLiteral("uint"), QStringLiteral("int")},
        {QStringLiteral("ulong"), QStringLiteral("int")}, {QStringLiteral("ushort"), QStringLiteral("int")},
        {QStringLiteral("float"), QStringLiteral("float")}, {QStringLiteral("double"), QStringLiteral("float")},
        {QStringLiteral("bool"), QStringLiteral("bool")}, {QStringLiteral("string"), QStringLiteral("String")},
        {QStringLiteral("String"), QStringLiteral("String")}, {QStringLiteral("Variant"), QStringLiteral("Variant")},
        {QStringLiteral("object"), QStringLiteral("Variant")}, {QStringLiteral("GodotObject"), QStringLiteral("Object")},
    };
    return map.value(t, t);
}

QList<ClassPtr> CSharpParser::parse(const QString &source, const QString &resPath, const Options &opts)
{
    Context ctx;
    ctx.resPath = resPath;
    ctx.opts = opts;
    parseBody(stripComments(source), nullptr, QString(), ctx);
    return ctx.classes;
}

QList<ClassPtr> CSharpParser::parseFile(const QString &absPath, const QString &resPath, const Options &opts)
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parse(QString::fromUtf8(f.readAll()), resPath, opts);
}
