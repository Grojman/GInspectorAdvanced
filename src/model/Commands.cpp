#include "Commands.h"

SetValueCommand::SetValueCommand(JsonDocument *doc, int item, const JPath &path, const JValue &value,
                                 const QString &text, bool mergeable)
    : QUndoCommand(text), m_doc(doc), m_item(item), m_path(path), m_old(doc->valueAt(item, path)), m_new(value),
      m_mergeable(mergeable)
{
    // When intermediate objects do not exist yet, store the change at the first
    // missing ancestor so undo removes everything this command created.
    for (int n = 1; n < path.size(); ++n) {
        const JPath prefix = path.mid(0, n);
        if (!doc->valueAt(item, prefix).isUndefined())
            continue;
        JValue built;
        if (setAt(built, path.mid(n), value)) {
            m_path = prefix;
            m_old = JValue();
            m_new = built;
        }
        break;
    }
    m_timer.start();
}

void SetValueCommand::undo()
{
    m_doc->setValueAt(m_item, m_path, m_old);
}

void SetValueCommand::redo()
{
    m_doc->setValueAt(m_item, m_path, m_new);
}

bool SetValueCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = static_cast<const SetValueCommand *>(other);
    if (!o->m_mergeable || o->m_doc != m_doc || o->m_item != m_item || !(o->m_path == m_path))
        return false;
    if (m_timer.elapsed() > 2500)
        return false;
    m_new = o->m_new;
    m_timer.restart();
    return true;
}

RenameItemCommand::RenameItemCommand(JsonDocument *doc, int item, const QString &newKey)
    : QUndoCommand(QStringLiteral("Rename %1 to %2").arg(doc->item(item).key, newKey)), m_doc(doc), m_item(item),
      m_old(doc->item(item).key), m_new(newKey)
{
}

void RenameItemCommand::undo()
{
    m_doc->setItemKey(m_item, m_old);
}

void RenameItemCommand::redo()
{
    m_doc->setItemKey(m_item, m_new);
}

ItemsCommand::ItemsCommand(JsonDocument *doc, const QList<DocItem> &after, const QString &text)
    : QUndoCommand(text), m_doc(doc), m_before(doc->items()), m_after(after)
{
}

void ItemsCommand::undo()
{
    m_doc->replaceItems(m_before);
}

void ItemsCommand::redo()
{
    m_doc->replaceItems(m_after);
}
