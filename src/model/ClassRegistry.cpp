#include "ClassRegistry.h"
#include "CSharpParser.h"
#include "GdScriptParser.h"
#include "ScriptText.h"
#include "core/JsonIO.h"
#include "core/Resources.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace {

JValue readJsonFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return JValue();
    return JsonIO::parse(QString::fromUtf8(f.readAll()));
}

const QHash<QString, VKind> &primitiveTypes()
{
    static const QHash<QString, VKind> m = {
        {QStringLiteral("Variant"), VKind::Variant}, {QStringLiteral("bool"), VKind::Bool},
        {QStringLiteral("int"), VKind::Int}, {QStringLiteral("float"), VKind::Float},
        {QStringLiteral("String"), VKind::String}, {QStringLiteral("StringName"), VKind::StringName},
        {QStringLiteral("NodePath"), VKind::NodePath}, {QStringLiteral("Vector2"), VKind::Vector2},
        {QStringLiteral("Vector2i"), VKind::Vector2i}, {QStringLiteral("Vector3"), VKind::Vector3},
        {QStringLiteral("Vector3i"), VKind::Vector3i}, {QStringLiteral("Vector4"), VKind::Vector4},
        {QStringLiteral("Vector4i"), VKind::Vector4i}, {QStringLiteral("Rect2"), VKind::Rect2},
        {QStringLiteral("Rect2i"), VKind::Rect2i}, {QStringLiteral("Color"), VKind::Color},
        {QStringLiteral("Array"), VKind::Array}, {QStringLiteral("Dictionary"), VKind::Dictionary},
    };
    return m;
}

const QHash<QString, QString> &packedArrays()
{
    static const QHash<QString, QString> m = {
        {QStringLiteral("PackedByteArray"), QStringLiteral("int")},
        {QStringLiteral("PackedInt32Array"), QStringLiteral("int")},
        {QStringLiteral("PackedInt64Array"), QStringLiteral("int")},
        {QStringLiteral("PackedFloat32Array"), QStringLiteral("float")},
        {QStringLiteral("PackedFloat64Array"), QStringLiteral("float")},
        {QStringLiteral("PackedStringArray"), QStringLiteral("String")},
        {QStringLiteral("PackedVector2Array"), QStringLiteral("Vector2")},
        {QStringLiteral("PackedVector3Array"), QStringLiteral("Vector3")},
        {QStringLiteral("PackedVector4Array"), QStringLiteral("Vector4")},
        {QStringLiteral("PackedColorArray"), QStringLiteral("Color")},
    };
    return m;
}

} // namespace

ClassRegistry::ClassRegistry()
{
    giaInitResources();
    loadBuiltins();
}

bool ClassRegistry::isValidExtensionApi(const QString &path)
{
    const JValue root = readJsonFile(path);
    return root.isObject() && root.value(QStringLiteral("classes")).isArray()
        && root.value(QStringLiteral("classes")).size() > 50;
}

void ClassRegistry::loadBuiltinJson(const JValue &root)
{
    for (const QString &k : std::as_const(m_builtinKeys))
        m_classes.remove(k);
    m_builtinKeys.clear();
    const JValue classes = root.value(QStringLiteral("classes"));
    for (const JValue &c : classes.elements()) {
        auto cls = std::make_shared<ClassInfo>();
        cls->builtin = true;
        cls->global = true;
        cls->language = QStringLiteral("builtin");
        cls->name = c.value(QStringLiteral("name")).toString();
        cls->key = cls->name;
        cls->baseRaw = c.value(QStringLiteral("inherits")).toString();
        cls->baseKey = cls->baseRaw;
        for (const JValue &p : c.ref(QStringLiteral("properties")).elements()) {
            PropertyInfo pi;
            pi.name = p.value(QStringLiteral("name")).toString();
            // "Texture2D,-AnimatedTexture" style lists: keep the first entry
            pi.typeRaw = p.value(QStringLiteral("type")).toString().section(QLatin1Char(','), 0, 0);
            if (pi.name.isEmpty() || pi.name.contains(QLatin1Char('/')))
                continue; // grouped/indexed pseudo properties
            cls->properties << pi;
        }
        for (const JValue &e : c.ref(QStringLiteral("enums")).elements()) {
            EnumInfo info;
            info.name = e.value(QStringLiteral("name")).toString();
            for (const JValue &v : e.ref(QStringLiteral("values")).elements())
                info.values << qMakePair(v.value(QStringLiteral("name")).toString(),
                                         v.value(QStringLiteral("value")).toInt());
            cls->enums.insert(info.name, info);
        }
        if (cls->name.isEmpty())
            continue;
        m_classes.insert(cls->key, cls);
        m_builtinKeys << cls->key;
    }
}

QString ClassRegistry::loadBuiltins(const QString &extensionApiPath)
{
    m_propCache.clear();
    if (!extensionApiPath.isEmpty() && isValidExtensionApi(extensionApiPath)) {
        const JValue root = readJsonFile(extensionApiPath);
        loadBuiltinJson(root);
        const JValue header = root.value(QStringLiteral("header"));
        m_builtinSource = QStringLiteral("Godot %1 (extension_api.json, %2 classes)")
                              .arg(header.value(QStringLiteral("version_full_name")).toString(QStringLiteral("?")))
                              .arg(m_builtinKeys.size());
        return m_builtinSource;
    }
    loadBuiltinJson(readJsonFile(QStringLiteral(":/builtin_classes.json")));
    m_builtinSource = QStringLiteral("Bundled class list (%1 common classes)").arg(m_builtinKeys.size());
    return m_builtinSource;
}

QString ClassRegistry::toRes(const QString &absPath) const
{
    if (m_root.isEmpty())
        return absPath;
    const QString rel = QDir(m_root).relativeFilePath(absPath);
    if (rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel))
        return QDir::fromNativeSeparators(absPath);
    return QStringLiteral("res://") + QDir::fromNativeSeparators(rel);
}

QString ClassRegistry::toAbs(const QString &resPath) const
{
    if (resPath.startsWith(QLatin1String("res://")))
        return QDir(m_root).absoluteFilePath(resPath.mid(6));
    return resPath;
}

void ClassRegistry::addClass(const ClassPtr &c)
{
    m_classes.insert(c->key, c);
    m_scriptKeys << c->key;
}

void ClassRegistry::scanProject()
{
    for (const QString &k : std::as_const(m_scriptKeys))
        m_classes.remove(k);
    m_scriptKeys.clear();
    m_pathToKey.clear();
    m_scriptTimes.clear();
    m_propCache.clear();
    if (m_root.isEmpty())
        return;

    QDirIterator it(m_root, {QStringLiteral("*.gd"), QStringLiteral("*.cs")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(m_root).relativeFilePath(path);
        if (rel.startsWith(QLatin1String(".godot/")) || rel.contains(QLatin1String("/.")) || rel.startsWith(QLatin1Char('.')))
            continue;
        if (rel.startsWith(QLatin1String("bin/")) || rel.startsWith(QLatin1String("obj/")))
            continue; // C# build output
        addScript(path);
    }
    resolveBases();
}

bool ClassRegistry::scriptsChanged() const
{
    for (auto it = m_scriptTimes.cbegin(); it != m_scriptTimes.cend(); ++it) {
        const QFileInfo fi(it.key());
        if (!fi.exists() || fi.lastModified() != it.value())
            return true;
    }
    return false;
}

QString ClassRegistry::addScript(const QString &absPath)
{
    const QString res = toRes(absPath);
    QList<ClassPtr> classes;
    if (absPath.endsWith(QLatin1String(".cs"), Qt::CaseInsensitive)) {
        CSharpParser::Options o;
        o.includePrivate = m_includePrivate;
        classes = CSharpParser::parseFile(absPath, res, o);
    } else {
        GdScriptParser::Options o;
        o.includePrivate = m_includePrivate;
        classes = GdScriptParser::parseFile(absPath, res, o);
    }
    m_scriptTimes.insert(absPath, QFileInfo(absPath).lastModified());
    if (classes.isEmpty())
        return QString();
    for (const ClassPtr &c : classes) {
        // A user class may not shadow a builtin; keep both reachable through its path.
        if (m_classes.contains(c->key) && m_builtinKeys.contains(c->key))
            c->key = res;
        if (m_classes.contains(c->key) && m_scriptKeys.contains(c->key)) {
            // Same key twice (duplicate class_name): replace the old one.
            m_scriptKeys.removeAll(c->key);
        }
        addClass(c);
    }
    const QString mainKey = classes.first()->key;
    m_pathToKey.insert(res, mainKey);
    resolveBases();
    m_propCache.clear();
    return mainKey;
}

void ClassRegistry::resolveBases()
{
    for (const QString &k : std::as_const(m_scriptKeys)) {
        ClassPtr c = m_classes.value(k);
        if (!c)
            continue;
        const ClassInfo *ctx = c->outerKey.isEmpty() ? c.get() : get(c->outerKey);
        const ClassInfo *b = lookup(c->baseRaw, ctx);
        c->baseKey = b && b != c.get() ? b->key : QString();
    }
}

const ClassInfo *ClassRegistry::get(const QString &key) const
{
    auto it = m_classes.constFind(key);
    return it == m_classes.cend() ? nullptr : it->get();
}

const ClassInfo *ClassRegistry::lookup(const QString &nameIn, const ClassInfo *ctx) const
{
    QString name = nameIn.trimmed();
    if (name.isEmpty())
        return nullptr;
    QString path;
    if (ScriptText::unquote(name, &path) || name.startsWith(QLatin1String("res://"))) {
        if (path.isEmpty())
            path = name;
        if (!path.startsWith(QLatin1String("res://")) && ctx && !ctx->scriptPath.isEmpty()) {
            // relative to the script's folder
            const QString dir = ctx->scriptPath.left(ctx->scriptPath.lastIndexOf(QLatin1Char('/')) + 1);
            path = QDir::cleanPath(dir + path).replace(QStringLiteral("res:/"), QStringLiteral("res://"));
        }
        const QString key = m_pathToKey.value(path);
        return key.isEmpty() ? nullptr : get(key);
    }

    if (name.contains(QLatin1Char('.'))) {
        // Outer.Inner (possibly nested)
        const QString head = name.section(QLatin1Char('.'), 0, 0);
        const QString rest = name.section(QLatin1Char('.'), 1);
        const ClassInfo *outer = lookup(head, ctx);
        if (!outer)
            return nullptr;
        const ClassInfo *found = get(outer->key + QLatin1Char('.') + rest.section(QLatin1Char('.'), 0, 0));
        if (found && rest.contains(QLatin1Char('.')))
            return lookup(rest.section(QLatin1Char('.'), 1), found);
        return found;
    }

    // Inner classes visible from ctx: its own, its outer classes', and inherited ones.
    for (const ClassInfo *scope = ctx; scope; scope = scope->outerKey.isEmpty() ? nullptr : get(scope->outerKey)) {
        int guard = 0;
        for (const ClassInfo *c = scope; c && guard < 64; c = c->baseKey.isEmpty() ? nullptr : get(c->baseKey), ++guard) {
            if (const ClassInfo *inner = get(c->key + QLatin1Char('.') + name))
                return inner;
        }
    }
    if (const ClassInfo *c = get(name))
        return c;
    return nullptr;
}

const ClassInfo *ClassRegistry::base(const ClassInfo *cls) const
{
    if (!cls || cls->baseKey.isEmpty())
        return nullptr;
    return get(cls->baseKey);
}

QList<const ClassInfo *> ClassRegistry::chain(const QString &key) const
{
    QList<const ClassInfo *> out;
    const ClassInfo *c = get(key);
    while (c && out.size() < 64 && !out.contains(c)) {
        out << c;
        c = base(c);
    }
    return out;
}

bool ClassRegistry::inherits(const QString &key, const QString &ancestorKey) const
{
    for (const ClassInfo *c : chain(key))
        if (c->key == ancestorKey)
            return true;
    return false;
}

QStringList ClassRegistry::descendants(const QString &key) const
{
    QStringList out;
    for (const QString &k : m_scriptKeys)
        if (k != key && inherits(k, key))
            out << k;
    std::sort(out.begin(), out.end());
    return out;
}

ClassRegistry::ObjectMode ClassRegistry::objectMode(const QString &key) const
{
    const ClassInfo *c = get(key);
    if (!c)
        return ObjectMode::Opaque;
    if (inherits(key, QStringLiteral("Node")))
        return ObjectMode::NodeRef;
    if (!c->builtin)
        return ObjectMode::Inline;
    if (inherits(key, QStringLiteral("Resource")))
        return ObjectMode::ResourcePath;
    return ObjectMode::Opaque;
}

QList<const ClassInfo *> ClassRegistry::scriptClasses() const
{
    QList<const ClassInfo *> out;
    for (const QString &k : m_scriptKeys)
        if (const ClassInfo *c = get(k))
            out << c;
    std::sort(out.begin(), out.end(), [](const ClassInfo *a, const ClassInfo *b) { return a->key < b->key; });
    return out;
}

QList<const ClassInfo *> ClassRegistry::builtinClasses() const
{
    QList<const ClassInfo *> out;
    for (const QString &k : m_builtinKeys)
        if (const ClassInfo *c = get(k))
            out << c;
    std::sort(out.begin(), out.end(), [](const ClassInfo *a, const ClassInfo *b) { return a->key < b->key; });
    return out;
}

const EnumInfo *ClassRegistry::findEnum(const QString &name, const ClassInfo *ctx, QString *enumKey) const
{
    if (name.contains(QLatin1Char('.'))) {
        const QString owner = name.section(QLatin1Char('.'), 0, -2);
        const QString en = name.section(QLatin1Char('.'), -1);
        const ClassInfo *c = lookup(owner, ctx);
        for (const ClassInfo *k : c ? chain(c->key) : QList<const ClassInfo *>()) {
            auto it = k->enums.constFind(en);
            if (it != k->enums.cend()) {
                if (enumKey)
                    *enumKey = k->key + QStringLiteral("::") + en;
                return &it.value();
            }
        }
        return nullptr;
    }
    for (const ClassInfo *scope = ctx; scope; scope = scope->outerKey.isEmpty() ? nullptr : get(scope->outerKey)) {
        for (const ClassInfo *k : chain(scope->key)) {
            auto it = k->enums.constFind(name);
            if (it != k->enums.cend()) {
                if (enumKey)
                    *enumKey = k->key + QStringLiteral("::") + name;
                return &it.value();
            }
        }
    }
    return nullptr;
}

const EnumInfo *ClassRegistry::enumInfo(const QString &enumKey) const
{
    const int sep = enumKey.lastIndexOf(QStringLiteral("::"));
    if (sep < 0)
        return nullptr;
    const ClassInfo *c = get(enumKey.left(sep));
    if (!c)
        return nullptr;
    auto it = c->enums.constFind(enumKey.mid(sep + 2));
    return it == c->enums.cend() ? nullptr : &it.value();
}

TypeRef ClassRegistry::resolveType(const QString &rawIn, const ClassInfo *ctx) const
{
    TypeRef t;
    QString raw = rawIn.trimmed();
    t.raw = raw;
    if (raw.isEmpty())
        return t;

    if (raw.startsWith(QLatin1String("enum::")) || raw.startsWith(QLatin1String("bitfield::"))) {
        t.kind = VKind::Int;
        QString key;
        if (findEnum(raw.section(QStringLiteral("::"), 1), ctx, &key))
            t.enumKey = key;
        return t;
    }
    if (raw.startsWith(QLatin1String("typedarray::")))
        raw = QStringLiteral("Array[%1]").arg(raw.mid(12));

    const int br = raw.indexOf(QLatin1Char('['));
    if (br > 0 && raw.endsWith(QLatin1Char(']'))) {
        const QString outer = raw.left(br).trimmed();
        const QStringList args = ScriptText::splitTopLevel(raw.mid(br + 1, raw.size() - br - 2));
        if (outer == QLatin1String("Array")) {
            t.kind = VKind::Array;
            t.elem = std::make_shared<TypeRef>(resolveType(args.value(0), ctx));
            return t;
        }
        if (outer == QLatin1String("Dictionary")) {
            t.kind = VKind::Dictionary;
            t.key = std::make_shared<TypeRef>(resolveType(args.value(0), ctx));
            t.elem = std::make_shared<TypeRef>(resolveType(args.value(1), ctx));
            return t;
        }
    }
    auto packed = packedArrays().constFind(raw);
    if (packed != packedArrays().cend()) {
        t.kind = VKind::Array;
        t.elem = std::make_shared<TypeRef>(resolveType(packed.value(), ctx));
        return t;
    }
    auto prim = primitiveTypes().constFind(raw);
    if (prim != primitiveTypes().cend()) {
        t.kind = prim.value();
        return t;
    }
    QString enumKey;
    // Prefer enums declared in the script scope before classes with the same name.
    if (findEnum(raw, ctx, &enumKey)) {
        t.kind = VKind::Int;
        t.enumKey = enumKey;
        return t;
    }
    if (const ClassInfo *c = lookup(raw, ctx)) {
        t.kind = VKind::Object;
        t.className = c->key;
        return t;
    }
    // Unknown type: accept anything but keep the raw name for display.
    t.kind = VKind::Variant;
    return t;
}

QString ClassRegistry::lookupIdentifier(const QString &ident, const ClassInfo *ctx) const
{
    // CONST, Enum.MEMBER, MEMBER (unnamed enum / enum member in scope), Class.CONST, Class.Enum.MEMBER
    const QStringList parts = ident.split(QLatin1Char('.'));
    for (const QString &p : parts)
        for (QChar ch : p)
            if (!ScriptText::isIdentChar(ch))
                return QString();

    auto constIn = [&](const ClassInfo *cls, const QString &name) -> QString {
        for (const ClassInfo *k : chain(cls->key)) {
            auto it = k->consts.constFind(name);
            if (it != k->consts.cend())
                return it.value();
        }
        return QString();
    };

    if (parts.size() == 1) {
        for (const ClassInfo *scope = ctx; scope; scope = scope->outerKey.isEmpty() ? nullptr : get(scope->outerKey)) {
            const QString c = constIn(scope, ident);
            if (!c.isEmpty())
                return c;
        }
        return QString();
    }
    const QString member = parts.last();
    const QString owner = parts.mid(0, parts.size() - 1).join(QLatin1Char('.'));
    // Enum.MEMBER
    if (const EnumInfo *e = findEnum(owner, ctx, nullptr)) {
        qint64 v = 0;
        if (e->valueOf(member, &v))
            return QString::number(v);
    }
    if (const ClassInfo *c = lookup(owner, ctx)) {
        const QString v = constIn(c, member);
        if (!v.isEmpty())
            return v;
    }
    return QString();
}

const QList<ResolvedProperty> &ClassRegistry::properties(const QString &key) const
{
    auto it = m_propCache.constFind(key);
    if (it != m_propCache.cend())
        return *it.value();

    auto outPtr = std::make_shared<QList<ResolvedProperty>>();
    QList<ResolvedProperty> &out = *outPtr;
    QSet<QString> seen;
    for (const ClassInfo *c : chain(key)) {
        for (const PropertyInfo &p : c->properties) {
            if (seen.contains(p.name))
                continue;
            seen.insert(p.name);
            ResolvedProperty rp;
            rp.info = p;
            rp.ownerKey = c->key;
            Values::EvalContext ectx;
            ectx.fmt = m_fmt;
            ectx.lookup = [this, c](const QString &id) { return lookupIdentifier(id, c); };
            if (!p.typeRaw.isEmpty()) {
                rp.type = resolveType(p.typeRaw, c);
            } else if (p.inferred || c->language == QLatin1String("csharp")) {
                TypeRef guess;
                Values::evalExpr(p.defaultExpr, TypeRef(), ectx, &guess);
                if (guess.kind == VKind::Object)
                    guess = resolveType(guess.raw, c);
                rp.type = guess;
            }
            // @export_enum on an int keeps int; on String the value is the option name.
            JValue def;
            if (!p.defaultExpr.isEmpty())
                def = Values::evalExpr(p.defaultExpr, rp.type, ectx);
            if (rp.type.kind == VKind::Object) {
                // Script class instances default to an inline object only when created with .new()
                if (!def.isObject())
                    def = JValue::null();
            }
            if (def.isUndefined())
                def = Values::typeDefault(rp.type, m_fmt);
            if (rp.type.kind == VKind::Int && def.isNumber())
                def = JValue(def.toInt());
            if (rp.type.kind == VKind::Float && def.isNumber())
                def = JValue(def.toDouble(), false);
            if (rp.type.kind == VKind::String && p.hint.kind == PropertyHint::Enum && def.isString()
                && def.toString().isEmpty() && !p.hint.options.isEmpty() && p.defaultExpr.isEmpty())
                def = JValue(p.hint.options.first().first);
            rp.defaultValue = def;
            out << rp;
        }
    }
    m_propCache.insert(key, outPtr);
    return *outPtr;
}

const ResolvedProperty *ClassRegistry::property(const QString &key, const QString &name) const
{
    for (const ResolvedProperty &p : properties(key))
        if (p.info.name == name)
            return &p;
    return nullptr;
}
