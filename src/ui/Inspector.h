#pragma once

#include "core/JValue.h"
#include "model/ClassRegistry.h"

#include <QHash>
#include <QPointer>
#include <QScrollArea>

class CollapsibleSection;
class JsonDocument;
class Project;
class QLabel;
class QToolButton;
class QVBoxLayout;
class Schema;
struct Issue;

// The right-hand panel: edits the fields of one item, like Godot's inspector.
// Every widget is built from the class definition so only declared fields can
// be added, and every editor enforces the field's type and restrictions.
class Inspector : public QScrollArea
{
    Q_OBJECT
public:
    explicit Inspector(Project *project, QWidget *parent = nullptr);

    void setTarget(JsonDocument *doc, int item);
    JsonDocument *document() const { return m_doc; }
    int item() const { return m_item; }
    void rebuild();
    void scheduleRebuild();
    // Expands the sections leading to `path` and scrolls to it.
    void reveal(const JPath &path);

signals:
    void statusMessage(const QString &text);
    // The user asked to link the JSON file to a different class.
    void changeClassRequested();

private:
    struct Row
    {
        JPath path;
        QPointer<QLabel> label;
        QPointer<QToolButton> revert;
        QString tooltip;
        bool revertable = true;
        QPointer<QWidget> anchor; // widget scrolled to by reveal()
        QPointer<CollapsibleSection> section;
    };

    // Value access for editors
    JValue stored(const JPath &p) const;
    void commit(const JPath &p, const JValue &v, const QString &label, bool mergeable = true);
    void commitStructural(const JPath &p, const JValue &v, const QString &label);
    // Remembers which sections are open, by path pattern, across rebuilds and items.
    void trackSection(CollapsibleSection *sec, const QString &key, bool def);

    void buildHeader(QVBoxLayout *into);
    void buildFields(QVBoxLayout *into, const JPath &base, const JValue &obj, const QString &classKey,
                     const QString &declaredKey, int depth, bool root);
    void buildUnknownKeys(QVBoxLayout *into, const JPath &base, const JValue &obj, const QString &classKey,
                          const QString &declaredKey, bool root);
    QWidget *buildProperty(const JPath &path, const ResolvedProperty &rp, int depth);
    // Builds the editor for a value; complex values become collapsible sections.
    QWidget *buildValue(const JPath &path, const QString &label, const TypeRef &t, const PropertyHint &h,
                        const JValue &def, int depth, Row &row, bool inArray);

    QWidget *simpleEditor(const JPath &path, const TypeRef &t, const PropertyHint &h, const JValue &eff,
                          const JValue &def);
    QWidget *intEditor(const JPath &path, const TypeRef &t, const PropertyHint &h, const JValue &eff);
    QWidget *floatEditor(const JPath &path, const PropertyHint &h, const JValue &eff);
    QWidget *stringEditor(const JPath &path, const TypeRef &t, const PropertyHint &h, const JValue &eff);
    QWidget *vectorEditor(const JPath &path, const TypeRef &t, const JValue &eff);
    QWidget *colorEditor(const JPath &path, const PropertyHint &h, const JValue &eff);
    QWidget *pathEditor(const JPath &path, const JValue &eff, const QStringList &filters, bool dirMode,
                        bool allowGlobal, bool nullable);
    QWidget *variantEditor(const JPath &path, const JValue &eff);

    CollapsibleSection *arraySection(const JPath &path, const QString &label, const TypeRef &t, const PropertyHint &h,
                                     int depth, Row &row);
    CollapsibleSection *dictSection(const JPath &path, const QString &label, const TypeRef &t, int depth, Row &row);
    CollapsibleSection *objectSection(const JPath &path, const QString &label, const TypeRef &t, int depth, Row &row,
                                      bool inArray);

    JValue newElement(const TypeRef &elem, const QString &classKey) const;
    QStringList resourceFilters(const QString &classKey) const;
    void refreshStates();
    void onItemChanged(int index);

    Project *m_project;
    Schema *m_schema;
    ClassRegistry *m_reg;
    JsonDocument *m_doc = nullptr;
    int m_item = -1;
    bool m_committing = false;
    bool m_rebuildPending = false;
    QPointer<QWidget> m_content;
    QList<Row> m_rows;
    QHash<QString, bool> m_expanded;      // pathPattern -> expanded
    QHash<QString, QStringList> m_issues; // pathToString -> messages
    QHash<QString, JValue> m_defaults;    // pathToString -> default value shown when absent
    QMetaObject::Connection m_conn1, m_conn2;
};
