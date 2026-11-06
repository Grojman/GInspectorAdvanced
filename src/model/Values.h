#pragma once

#include "Types.h"
#include "core/JValue.h"

#include <QColor>
#include <QVector>
#include <functional>

// How Godot value types (Vector2, Color...) are written in the JSON files.
struct ValueFormat
{
    enum Vector { VectorArray, VectorObject, VectorString };
    enum ColorFmt { ColorHex, ColorArray, ColorObject };
    Vector vector = VectorArray;
    ColorFmt color = ColorHex;

    static QString vectorName(Vector v);
    static Vector vectorFromName(const QString &s);
    static QString colorName(ColorFmt c);
    static ColorFmt colorFromName(const QString &s);
};

namespace Values {

QStringList componentNames(const TypeRef &t); // x,y / x,y,z / x,y,w,h / r,g,b,a

// Reads a vector/rect in any accepted form: [1, 2], {"x": 1, "y": 2}, "(1, 2)".
bool readVector(const JValue &v, const TypeRef &t, QVector<double> *out);
// Writes using the same form as `like` when it is a valid vector, else `fmt`.
JValue writeVector(const QVector<double> &vals, const TypeRef &t, const ValueFormat &fmt, const JValue &like = JValue());

// Colors: "#rrggbb", "#rrggbbaa", "rgb", [r, g, b(, a)], {"r":..,"g":..,"b":..(,"a")}
bool readColor(const JValue &v, QColor *out);
JValue writeColor(const QColor &c, const ValueFormat &fmt, bool alpha, const JValue &like = JValue());

JValue typeDefault(const TypeRef &t, const ValueFormat &fmt);

// Evaluates a GDScript/C# default value expression. `lookup` resolves bare
// identifiers (constants, enum members) to expressions or numbers.
struct EvalContext
{
    ValueFormat fmt;
    std::function<QString(const QString &)> lookup; // returns replacement expression or empty
    int depth = 0;
};
// Returns Undefined when the expression is not a literal it understands.
// `guess` receives the kind of the literal (for ':=' inferred types).
JValue evalExpr(const QString &expr, const TypeRef &t, EvalContext &ctx, TypeRef *guess = nullptr);

} // namespace Values
