#pragma once

#include "Values.h"

#include <QMap>
#include <QPair>
#include <QString>

// Which class a JSON file is made of, and how the items are stored in it.
struct LinkInfo
{
    QString json;       // res:// path of the JSON file
    QString classKey;   // registry key of the item class
    QString scriptPath; // res:// path of the script (empty for builtin classes)
    QString shape;      // "dictionary" ({id: item}), "array" ([item, ...]) or "single" (one item)
    QString keyField;   // dictionary shape: class field filled from the key (e.g. "id"), hidden in the inspector
};

// How subclasses of a base class are told apart inside JSON objects:
// {"type": "alter_stat", ...} -> AlterStatEffect
struct PolyConfig
{
    QString key = QStringLiteral("type");
    QList<QPair<QString, QString>> types; // json value -> class key
};

// The per-project file (ginspector.json, at the project root) that remembers
// links, polymorphism and format settings.
class ProjectConfig
{
public:
    static QString fileName() { return QStringLiteral("ginspector.json"); }

    bool load(const QString &projectRoot, QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    QString filePath() const;

    QString root;
    QMap<QString, LinkInfo> links;   // keyed by res:// json path
    QMap<QString, PolyConfig> poly;  // keyed by base class key
    ValueFormat format;
    bool includePrivate = false;
    bool backups = true;
};
