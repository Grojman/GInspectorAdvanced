#pragma once

#include <QString>
#include <QStringList>

// Small helpers shared by the script parsers to scan code while respecting
// strings and brackets.
namespace ScriptText {

// Index of the first occurrence of any char of `chars` at bracket depth 0 and
// outside string literals, starting at `from`. -1 if none.
int findTopLevel(const QString &s, const QString &chars, int from = 0);

// Like findTopLevel but for a two-char token such as ":=".
int findTopLevelToken(const QString &s, const QString &token, int from = 0);

// Splits on commas at depth 0 (outside strings and brackets).
QStringList splitTopLevel(const QString &s, QChar sep = QLatin1Char(','));

// Index just after the bracket matching the one at `open`, or -1.
int matchBracket(const QString &s, int open);

// If `s` (trimmed) is a single string literal (any GDScript/C# quote style,
// including &"..." and ^"..." and @"..."), returns its content.
bool unquote(const QString &s, QString *out);

bool isIdentStart(QChar c);
bool isIdentChar(QChar c);
// Reads an identifier at `pos`, advancing it. Empty if none.
QString readIdent(const QString &s, int &pos);
void skipSpaces(const QString &s, int &pos);

} // namespace ScriptText
