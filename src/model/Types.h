#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <memory>

// Kinds of values a Godot property can hold, as far as JSON editing is concerned.
enum class VKind {
    Variant,   // untyped: anything goes
    Bool,
    Int,
    Float,
    String,
    StringName,
    NodePath,
    Vector2,
    Vector2i,
    Vector3,
    Vector3i,
    Vector4,
    Vector4i,
    Rect2,
    Rect2i,
    Color,
    Array,      // elem may be null (untyped)
    Dictionary, // key/elem may be null (untyped)
    Object,     // className set: a class from the registry
};

struct TypeRef
{
    VKind kind = VKind::Variant;
    QString className;  // Object: registry key of the class
    QString enumKey;    // Int: "OwnerKey::EnumName" if typed as an enum
    std::shared_ptr<TypeRef> elem; // Array element / Dictionary value
    std::shared_ptr<TypeRef> key;  // Dictionary key
    QString raw;        // the type as written in the script

    bool isVector() const;
    int components() const; // vectors, rects, color
    bool isIntVector() const;
    bool isNumeric() const { return kind == VKind::Int || kind == VKind::Float; }
    bool isStringLike() const { return kind == VKind::String || kind == VKind::StringName || kind == VKind::NodePath; }
    QString displayName() const;
};

struct EnumInfo
{
    QString name;
    QList<QPair<QString, qint64>> values;
    QString nameOf(qint64 v) const;
    bool hasValue(qint64 v) const;
    bool valueOf(const QString &n, qint64 *out) const;
};

// Restrictions coming from @export_* annotations (or C# [Export] hints).
struct PropertyHint
{
    enum Kind { None, Range, Enum, Flags, File, Dir, Multiline, Placeholder, ColorNoAlpha, NodePathTypes, ExpEasing };
    Kind kind = None;
    bool hasMin = false, hasMax = false;
    double min = 0, max = 0, step = 0;
    bool orGreater = false, orLess = false, hideSlider = false, degrees = false;
    QString suffix;
    QList<QPair<QString, qint64>> options; // enum / flags (value is the bit for flags)
    QStringList filters;                   // file filters, e.g. "*.png"
    bool globalPath = false;
    QString placeholder;
};

struct PropertyInfo
{
    QString name;
    QString typeRaw;     // as written, may be empty (untyped / inferred)
    QString defaultExpr; // source expression after '=' or ':='
    bool inferred = false; // declared with ':='
    bool exported = false;
    QString group;
    QString subgroup;
    QString doc;
    PropertyHint hint;
    int line = 0;
};

struct ClassInfo
{
    QString key;        // registry key: class_name, builtin name, res:// path, or Outer.Inner
    QString name;       // display name
    QString scriptPath; // res:// path of the source file (empty for builtins)
    QString baseRaw;    // as written after extends (name or path)
    QString baseKey;    // resolved by the registry
    QString language;   // "gdscript", "csharp" or "builtin"
    bool builtin = false;
    bool global = false; // has class_name / is a global C# class
    QString outerKey;    // for inner classes
    QStringList innerKeys;
    QList<PropertyInfo> properties;
    QHash<QString, EnumInfo> enums;
    QHash<QString, QString> consts; // name -> expression
    QString iconPath;
    QString doc;
    QStringList parseWarnings;
};

using ClassPtr = std::shared_ptr<ClassInfo>;
