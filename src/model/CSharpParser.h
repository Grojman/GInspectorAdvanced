#pragma once

#include "Types.h"

// Extracts classes, enums and fields/auto-properties from a Godot C# script.
// Types are translated to their GDScript spelling (float, Array[T], ...)
// so the registry resolves them the same way.
class CSharpParser
{
public:
    struct Options
    {
        bool includePrivate = false; // include non-public members without [Export]
    };
    static QList<ClassPtr> parse(const QString &source, const QString &resPath, const Options &opts);
    static QList<ClassPtr> parseFile(const QString &absPath, const QString &resPath, const Options &opts);
    static QString translateType(const QString &csType);
};
