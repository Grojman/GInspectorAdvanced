#pragma once

#include "JsonDocument.h"

#include <QElapsedTimer>
#include <QUndoCommand>

// Changes one value inside an item. Consecutive edits of the same field
// (typing, dragging a slider) merge into a single undo step.
class SetValueCommand : public QUndoCommand
{
public:
    SetValueCommand(JsonDocument *doc, int item, const JPath &path, const JValue &value, const QString &text,
                    bool mergeable);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeable ? 1 : -1; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    JsonDocument *m_doc;
    int m_item;
    JPath m_path;
    JValue m_old;
    JValue m_new;
    bool m_mergeable;
    QElapsedTimer m_timer;
};

// Renames a dictionary key.
class RenameItemCommand : public QUndoCommand
{
public:
    RenameItemCommand(JsonDocument *doc, int item, const QString &newKey);
    void undo() override;
    void redo() override;

private:
    JsonDocument *m_doc;
    int m_item;
    QString m_old;
    QString m_new;
};

// Replaces the whole item list (add, delete, duplicate, reorder, fix all).
class ItemsCommand : public QUndoCommand
{
public:
    ItemsCommand(JsonDocument *doc, const QList<DocItem> &after, const QString &text);
    void undo() override;
    void redo() override;

private:
    JsonDocument *m_doc;
    QList<DocItem> m_before;
    QList<DocItem> m_after;
};
