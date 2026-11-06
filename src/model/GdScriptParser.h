#pragma once

#include "Types.h"

// Extracts the class-level layout of a GDScript file: class_name, extends,
// variables with their annotations, enums, constants and inner classes.
// It is a declarative scanner, not a full GDScript parser: function bodies
// are skipped.
class GdScriptParser
{
public:
    struct Options
    {
        bool includePrivate = false; // include vars whose name starts with '_'
    };

    // Returns the main class first, followed by every inner class (keys "Outer.Inner").
    // `resPath` is the res:// path used for anonymous scripts and inner class keys.
    static QList<ClassPtr> parse(const QString &source, const QString &resPath, const Options &opts);
    static QList<ClassPtr> parseFile(const QString &absPath, const QString &resPath, const Options &opts);
};
