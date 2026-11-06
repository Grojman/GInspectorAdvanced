#pragma once

#include "ProjectConfig.h"
#include "core/JValue.h"

#include <QDateTime>
#include <QObject>
#include <QSet>

class QUndoStack;

struct DocItem
{
    QString key;  // dictionary shape only
    JValue value;
};

// A JSON data file opened in the editor, split into items.
class JsonDocument : public QObject
{
    Q_OBJECT
public:
    enum class Shape { Dictionary, Array, Single };

    JsonDocument(const QString &absPath, const QString &resPath, QObject *parent = nullptr);

    // Reads and parses the file. On failure `error` holds the message and line.
    bool load(QString *error);
    // Splits the parsed root into items according to the link (shape may be empty: auto-detect).
    void applyLink(const LinkInfo &link, const QStringList &classFields);
    bool save(bool backup, QString *error);
    // True when the file changed on disk since it was loaded/saved.
    bool changedOnDisk() const;

    static Shape detectShape(const JValue &root, const QStringList &classFields);
    static QString shapeName(Shape s);
    static Shape shapeFromName(const QString &s, Shape def);

    QString absPath() const { return m_absPath; }
    QString resPath() const { return m_resPath; }
    QString fileName() const;
    const LinkInfo &link() const { return m_link; }
    // Edited as plain JSON: no class, no validation.
    bool isPlain() const { return m_link.plain; }
    void setLink(const LinkInfo &l) { m_link = l; }
    Shape shape() const { return m_shape; }
    const JValue &root() const { return m_root; }

    int count() const { return m_items.size(); }
    const DocItem &item(int i) const { return m_items[i]; }
    const QList<DocItem> &items() const { return m_items; }
    QString itemTitle(int i) const;
    QString uniqueKey(const QString &base) const;

    JValue valueAt(int item, const JPath &path) const;
    // Raw mutators used by the undo commands (no undo entry is created here).
    void setValueAt(int item, const JPath &path, const JValue &v);
    void setItemKey(int item, const QString &key);
    void replaceItems(const QList<DocItem> &items);

    QUndoStack *undoStack() const { return m_undo; }
    bool isModified() const;
    bool isItemModified(int i) const { return m_modifiedItems.contains(i); }

signals:
    void itemChanged(int index);
    void itemsReset();
    void modifiedChanged(bool modified);

private:
    JValue buildRoot() const;

    QString m_absPath;
    QString m_resPath;
    LinkInfo m_link;
    Shape m_shape = Shape::Dictionary;
    JValue m_root;
    QList<DocItem> m_items;
    QString m_indent = QStringLiteral("\t");
    bool m_trailingNewline = true;
    QDateTime m_fileTime;
    QUndoStack *m_undo;
    QSet<int> m_modifiedItems;
};
