#pragma once

#include "JValue.h"

struct JsonParseError
{
    QString message;
    int line = 0;
    int column = 0;
    bool ok() const { return message.isEmpty(); }
};

namespace JsonIO {

// Parses standard JSON (RFC 8259), keeping key order and number lexemes.
JValue parse(const QString &text, JsonParseError *error = nullptr);

struct WriteOptions
{
    QString indent = QStringLiteral("\t");
    // Containers deeper than this are written on one line when they fit.
    int expandDepth = 1;
    // A container whose children are all scalars is written inline if it fits in this many chars.
    int inlineLimit = 100;
    // An array element (e.g. an effect object) is written inline if it fits in this many chars.
    int elementInlineLimit = 160;
    bool trailingNewline = true;
};

QString write(const JValue &root, const WriteOptions &opts = WriteOptions());
// Compact single-line form ("{"a": 1, "b": [1, 2]}").
QString writeCompact(const JValue &v);
QString formatNumber(const JValue &v);
QString quote(const QString &s);

// Guesses the indentation unit used by an existing JSON text ("\t" by default).
QString detectIndent(const QString &text);

} // namespace JsonIO
