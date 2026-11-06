#include "Types.h"

bool TypeRef::isVector() const
{
    switch (kind) {
    case VKind::Vector2: case VKind::Vector2i: case VKind::Vector3: case VKind::Vector3i:
    case VKind::Vector4: case VKind::Vector4i: case VKind::Rect2: case VKind::Rect2i:
        return true;
    default:
        return false;
    }
}

int TypeRef::components() const
{
    switch (kind) {
    case VKind::Vector2: case VKind::Vector2i: return 2;
    case VKind::Vector3: case VKind::Vector3i: return 3;
    case VKind::Vector4: case VKind::Vector4i: case VKind::Rect2: case VKind::Rect2i: case VKind::Color: return 4;
    default: return 0;
    }
}

bool TypeRef::isIntVector() const
{
    return kind == VKind::Vector2i || kind == VKind::Vector3i || kind == VKind::Vector4i || kind == VKind::Rect2i;
}

QString TypeRef::displayName() const
{
    switch (kind) {
    case VKind::Variant: return QStringLiteral("Variant");
    case VKind::Bool: return QStringLiteral("bool");
    case VKind::Int: return enumKey.isEmpty() ? QStringLiteral("int") : enumKey.section(QStringLiteral("::"), -1);
    case VKind::Float: return QStringLiteral("float");
    case VKind::String: return QStringLiteral("String");
    case VKind::StringName: return QStringLiteral("StringName");
    case VKind::NodePath: return QStringLiteral("NodePath");
    case VKind::Vector2: return QStringLiteral("Vector2");
    case VKind::Vector2i: return QStringLiteral("Vector2i");
    case VKind::Vector3: return QStringLiteral("Vector3");
    case VKind::Vector3i: return QStringLiteral("Vector3i");
    case VKind::Vector4: return QStringLiteral("Vector4");
    case VKind::Vector4i: return QStringLiteral("Vector4i");
    case VKind::Rect2: return QStringLiteral("Rect2");
    case VKind::Rect2i: return QStringLiteral("Rect2i");
    case VKind::Color: return QStringLiteral("Color");
    case VKind::Array:
        return elem ? QStringLiteral("Array[%1]").arg(elem->displayName()) : QStringLiteral("Array");
    case VKind::Dictionary:
        if (key || elem)
            return QStringLiteral("Dictionary[%1, %2]")
                .arg(key ? key->displayName() : QStringLiteral("Variant"),
                     elem ? elem->displayName() : QStringLiteral("Variant"));
        return QStringLiteral("Dictionary");
    case VKind::Object: {
        const int dot = className.lastIndexOf(QLatin1Char('/'));
        return dot >= 0 ? className.mid(dot + 1) : className;
    }
    }
    return raw;
}

QString EnumInfo::nameOf(qint64 v) const
{
    for (const auto &p : values)
        if (p.second == v)
            return p.first;
    return QString();
}

bool EnumInfo::hasValue(qint64 v) const
{
    for (const auto &p : values)
        if (p.second == v)
            return true;
    return false;
}

bool EnumInfo::valueOf(const QString &n, qint64 *out) const
{
    for (const auto &p : values)
        if (p.first == n) {
            if (out)
                *out = p.second;
            return true;
        }
    return false;
}
