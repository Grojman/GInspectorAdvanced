#pragma once

#include "Types.h"
#include "Values.h"
#include "core/JValue.h"

#include <QDateTime>
#include <QHash>

// A property of a class after its type and default value have been resolved.
struct ResolvedProperty
{
    PropertyInfo info;
    TypeRef type;
    JValue defaultValue;
    QString ownerKey; // class that declares it
};

// All classes known for a project: built-in Godot classes plus every script
// class found in the project. Resolves names, inheritance and property types.
class ClassRegistry
{
public:
    enum class ObjectMode {
        Inline,       // script class (Resource/RefCounted/Object): nested JSON object
        ResourcePath, // built-in resource (Texture2D...): "res://" path string
        NodeRef,      // Node-derived: NodePath string
        Opaque,       // anything else: free value
    };

    ClassRegistry();

    // Built-in classes: from a Godot extension_api.json (if given and valid) or
    // the curated list bundled with the app. Returns a description of the source.
    QString loadBuiltins(const QString &extensionApiPath = QString());
    static bool isValidExtensionApi(const QString &path);

    void setProjectRoot(const QString &root) { m_root = root; }
    QString projectRoot() const { return m_root; }
    void setFormat(const ValueFormat &fmt) { m_fmt = fmt; m_propCache.clear(); }
    const ValueFormat &format() const { return m_fmt; }
    void setIncludePrivate(bool v) { m_includePrivate = v; }

    // Parses every .gd / .cs script under the project root (skipping .godot/).
    void scanProject();
    // True when scripts changed on disk since the last scan.
    bool scriptsChanged() const;
    // Parses a single script (also outside the project). Returns its main class key.
    QString addScript(const QString &absPath);

    QString toRes(const QString &absPath) const;
    QString toAbs(const QString &resPath) const;

    const ClassInfo *get(const QString &key) const;
    // Resolves a class name as written in `ctx` (inner classes, globals, builtins, paths).
    const ClassInfo *lookup(const QString &name, const ClassInfo *ctx = nullptr) const;
    const ClassInfo *base(const ClassInfo *cls) const;
    // The class followed by its ancestors, up to Object.
    QList<const ClassInfo *> chain(const QString &key) const;
    bool inherits(const QString &key, const QString &ancestorKey) const;
    // Script classes deriving from key (directly or not).
    QStringList descendants(const QString &key) const;
    ObjectMode objectMode(const QString &key) const;

    QList<const ClassInfo *> scriptClasses() const;
    QList<const ClassInfo *> builtinClasses() const;

    // Properties of a class including inherited ones, most-derived class first.
    const QList<ResolvedProperty> &properties(const QString &key) const;
    const ResolvedProperty *property(const QString &key, const QString &name) const;

    TypeRef resolveType(const QString &raw, const ClassInfo *ctx) const;
    const EnumInfo *enumInfo(const QString &enumKey) const;

    QString builtinSource() const { return m_builtinSource; }

private:
    void addClass(const ClassPtr &c);
    void resolveBases();
    const EnumInfo *findEnum(const QString &name, const ClassInfo *ctx, QString *enumKey) const;
    QString lookupIdentifier(const QString &ident, const ClassInfo *ctx) const;
    void loadBuiltinJson(const JValue &root);

    QString m_root;
    ValueFormat m_fmt;
    bool m_includePrivate = false;
    QHash<QString, ClassPtr> m_classes;   // key -> class (scripts and builtins)
    QHash<QString, QString> m_pathToKey;  // res:// path -> main class key
    QHash<QString, QDateTime> m_scriptTimes;
    QStringList m_builtinKeys;
    QStringList m_scriptKeys;
    QString m_builtinSource;
    mutable QHash<QString, std::shared_ptr<QList<ResolvedProperty>>> m_propCache;
};
