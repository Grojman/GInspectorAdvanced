#include "Inspector.h"
#include "Widgets.h"
#include "Theme.h"
#include "core/JsonIO.h"
#include "model/Commands.h"
#include "model/Project.h"
#include "model/Validator.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace {

QString prettify(const QString &name)
{
    QStringList words;
    QString cur;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name[i];
        if (c == QLatin1Char('_')) {
            if (!cur.isEmpty()) words << cur;
            cur.clear();
            continue;
        }
        if (c.isUpper() && !cur.isEmpty() && cur.back().isLower()) {
            words << cur;
            cur.clear();
        }
        cur += c;
    }
    if (!cur.isEmpty())
        words << cur;
    for (QString &w : words)
        if (!w.isEmpty())
            w[0] = w[0].toUpper();
    return words.join(QLatin1Char(' '));
}

QString fmtNum(double d)
{
    return QString::number(d, 'g', 10);
}

QString hintText(const PropertyHint &h)
{
    switch (h.kind) {
    case PropertyHint::Range: {
        QString s = QStringLiteral("Range: %1 – %2").arg(h.hasMin ? fmtNum(h.min) : QStringLiteral("−∞"),
                                                         h.hasMax ? fmtNum(h.max) : QStringLiteral("∞"));
        if (h.step > 0) s += QStringLiteral(", step %1").arg(fmtNum(h.step));
        if (h.orGreater) s += QStringLiteral(", or greater");
        if (h.orLess) s += QStringLiteral(", or less");
        return s;
    }
    case PropertyHint::Enum: {
        QStringList o;
        for (const auto &p : h.options) o << p.first;
        return QStringLiteral("Options: ") + o.join(QStringLiteral(", "));
    }
    case PropertyHint::Flags: return QStringLiteral("Flags");
    case PropertyHint::File:
        return QStringLiteral("File") + (h.filters.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(h.filters.join(QStringLiteral(", "))));
    case PropertyHint::Dir: return QStringLiteral("Directory");
    case PropertyHint::Multiline: return QStringLiteral("Multiline text");
    default: return QString();
    }
}

int labelWidth(int depth)
{
    return qMax(120, Theme::LabelWidth - depth * 18);
}

QLabel *makeLabel(const QString &text, int depth, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setFixedWidth(labelWidth(depth));
    l->setTextInteractionFlags(Qt::NoTextInteraction);
    return l;
}

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

bool isComplex(const TypeRef &t, ClassRegistry *reg)
{
    if (t.kind == VKind::Array || t.kind == VKind::Dictionary)
        return true;
    if (t.kind == VKind::Object)
        return reg->objectMode(t.className) == ClassRegistry::ObjectMode::Inline;
    return false;
}

const QStringList &imageExts()
{
    static const QStringList e = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                  QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("bmp"),
                                  QStringLiteral("tga")};
    return e;
}

} // namespace

Inspector::Inspector(Project *project, QWidget *parent)
    : QScrollArea(parent), m_project(project), m_schema(&project->schema()), m_reg(&project->registry())
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setObjectName(QStringLiteral("inspector"));
    setStyleSheet(QStringLiteral(
        "QLabel[state=\"error\"] { color: #e5534b; font-weight: bold; }"
        "QLabel[state=\"warning\"] { color: #d29922; }"
        "QLabel[state=\"child-error\"] { color: #e5534b; }"
        "QLabel[state=\"absent\"] { color: palette(placeholder-text); }"
        "QLabel#dimLabel { color: palette(placeholder-text); }"
        "QWidget#categoryHeader { background: palette(midlight); border-radius: 7px; }"
        "QWidget#groupHeader { border-bottom: 2px solid palette(mid); }"
        "QWidget#itemHeader { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 10px; }"
        "QFrame#unknownBox { border: 2px solid #e5534b; border-radius: 8px; }"
        "QLineEdit[invalid=\"true\"] { border: 1px solid #e5534b; }"));
    rebuild();
}

void Inspector::setTarget(JsonDocument *doc, int item)
{
    if (doc != m_doc) {
        disconnect(m_conn1);
        disconnect(m_conn2);
        m_doc = doc;
        if (doc) {
            m_conn1 = connect(doc, &JsonDocument::itemChanged, this, &Inspector::onItemChanged);
            m_conn2 = connect(doc, &JsonDocument::itemsReset, this, &Inspector::scheduleRebuild);
        }
    }
    m_item = item;
    rebuild();
    verticalScrollBar()->setValue(0);
}

void Inspector::onItemChanged(int index)
{
    if (index == m_item && !m_committing)
        scheduleRebuild();
}

void Inspector::scheduleRebuild()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildPending = false;
        rebuild();
    });
}

JValue Inspector::stored(const JPath &p) const
{
    return m_doc ? m_doc->valueAt(m_item, p) : JValue();
}

void Inspector::commit(const JPath &pathIn, const JValue &v, const QString &label, bool mergeable)
{
    if (!m_doc || m_item < 0 || m_item >= m_doc->count())
        return;
    JPath path = pathIn;
    JValue value = v;
    // If an ancestor is absent but shown with its default (a default array or an
    // object created with .new()), store the default with the change applied.
    for (int n = 1; n < pathIn.size(); ++n) {
        const JPath prefix = pathIn.mid(0, n);
        if (!stored(prefix).isUndefined())
            continue;
        const auto it = m_defaults.constFind(pathToString(prefix));
        if (it == m_defaults.cend() || !it->isContainer())
            break;
        JValue base = *it;
        if (!setAt(base, pathIn.mid(n), v))
            break;
        path = prefix;
        value = base;
        break;
    }
    if (stored(path) == value && stored(path).isUndefined() == value.isUndefined())
        return;
    m_committing = true;
    m_doc->undoStack()->push(new SetValueCommand(m_doc, m_item, path, value, label, mergeable));
    m_committing = false;
    refreshStates();
}

void Inspector::commitStructural(const JPath &p, const JValue &v, const QString &label)
{
    commit(p, v, label, false);
    scheduleRebuild();
}

void Inspector::trackSection(CollapsibleSection *sec, const QString &key, bool def)
{
    sec->setProperty("stateKey", key);
    sec->setExpanded(m_expanded.value(key, def));
    connect(sec, &CollapsibleSection::toggled, this, [this, key](bool e) { m_expanded.insert(key, e); });
}

void Inspector::rebuild()
{
    const int scroll = verticalScrollBar()->value();
    QWidget *focus = nullptr;
    m_rows.clear();
    m_defaults.clear();

    auto *content = new QWidget();
    m_content = content; // parent for widgets created while building
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(Theme::PageMargin, Theme::PageMargin, Theme::PageMargin, Theme::PageMargin);
    layout->setSpacing(Theme::SectionSpacing);

    const bool plain = m_doc && m_doc->isPlain();
    if (m_doc && !plain && (m_doc->link().classKey.isEmpty() || !m_reg->get(m_doc->link().classKey))) {
        buildUnlinked(layout);
    } else if (!m_doc || m_item < 0 || m_item >= m_doc->count()) {
        auto *l = new QLabel(m_doc ? tr("Select an item to edit its values.")
                                   : tr("No JSON file opened.\nPick one in the Files panel or with File ▸ Open JSON…"),
                             content);
        l->setAlignment(Qt::AlignCenter);
        l->setObjectName(QStringLiteral("dimLabel"));
        layout->addStretch(1);
        layout->addWidget(l);
        layout->addStretch(2);
    } else if (plain) {
        buildPlainHeader(layout);
        const JValue itemVal = m_doc->item(m_item).value;
        if (itemVal.isContainer()) {
            buildPlainBody(layout, {}, itemVal, 0);
        } else {
            layout->addWidget(plainValue({}, makeLabel(tr("Value"), 0, content), itemVal, 0, {}));
        }
        layout->addStretch(1);
    } else {
        buildHeader(layout);
        const JValue itemVal = m_doc->item(m_item).value;
        if (itemVal.isObject()) {
            const QString declared = m_doc->link().classKey;
            const QString actual = m_schema->actualClass(itemVal, declared);
            buildFields(layout, {}, itemVal, actual, declared, 0, true);
        }
        layout->addStretch(1);
    }
    focus = content;
    QWidget *old = takeWidget();
    setWidget(content);
    if (old)
        old->deleteLater();
    refreshStates();
    QTimer::singleShot(0, this, [this, scroll] { verticalScrollBar()->setValue(scroll); });
    Q_UNUSED(focus);
}

QWidget *Inspector::itemKeyEditor(QWidget *parent)
{
    auto *key = new QLineEdit(m_doc->item(m_item).key, parent);
    key->setToolTip(tr("The key of this item in the JSON object. Must be unique."));
    connect(key, &QLineEdit::editingFinished, this, [this, key] {
        const QString k = key->text().trimmed();
        const QString old = m_doc->item(m_item).key;
        if (k == old)
            return;
        QString err;
        if (k.isEmpty())
            err = tr("The key cannot be empty.");
        for (int i = 0; i < m_doc->count() && err.isEmpty(); ++i)
            if (i != m_item && m_doc->item(i).key == k)
                err = tr("Another item already uses the key \"%1\".").arg(k);
        if (!err.isEmpty()) {
            key->setText(old);
            emit statusMessage(err);
            return;
        }
        m_committing = true;
        m_doc->undoStack()->push(new RenameItemCommand(m_doc, m_item, k));
        m_committing = false;
        scheduleRebuild();
    });
    return key;
}

void Inspector::buildHeader(QVBoxLayout *into)
{
    QWidget *parent = into->parentWidget();
    const DocItem &it = m_doc->item(m_item);
    const QString declared = m_doc->link().classKey;
    const QString actual = m_schema->actualClass(it.value, declared);

    auto *box = new QWidget(parent);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(16, 14, 16, 14);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(Theme::RowSpacing);
    box->setObjectName(QStringLiteral("itemHeader"));
    box->setAttribute(Qt::WA_StyledBackground, true);
    int r = 0;

    auto *title = new QLabel(m_doc->itemTitle(m_item), box);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.6);
    tf.setBold(true);
    title->setFont(tf);
    grid->addWidget(title, r++, 0, 1, 3);

    if (m_doc->shape() == JsonDocument::Shape::Dictionary) {
        grid->addWidget(makeLabel(m_doc->link().keyField.isEmpty() ? tr("Key")
                                                                    : tr("Key (%1)").arg(m_doc->link().keyField),
                                  0, box),
                        r, 0);
        grid->addWidget(itemKeyEditor(box), r++, 1, 1, 2);
    }

    grid->addWidget(makeLabel(tr("Class"), 0, box), r, 0);
    QStringList chainNames;
    for (const ClassInfo *c : m_reg->chain(actual))
        chainNames << m_schema->displayName(c->key);
    QStringList assignable = it.value.isObject() ? m_schema->assignableClasses(declared) : QStringList();
    if (assignable.size() > 1) {
        auto *combo = new QComboBox(box);
        WheelGuard::install(combo);
        for (const QString &k : assignable)
            combo->addItem(m_schema->displayName(k), k);
        combo->setCurrentIndex(qMax(0, assignable.indexOf(actual)));
        combo->setToolTip(chainNames.join(QStringLiteral(" ‹ ")));
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo, declared](int) {
            const QString cls = combo->currentData().toString();
            const JValue old = m_doc->item(m_item).value;
            JValue nv = m_schema->newObject(cls, declared);
            for (const JMember &m : old.members())
                if (m_reg->property(cls, m.key))
                    nv.set(m.key, m.value);
            commitStructural({}, nv, tr("Change class"));
        });
        grid->addWidget(combo, r, 1);
    } else {
        auto *cl = new QLabel(chainNames.join(QStringLiteral(" ‹ ")), box);
        cl->setWordWrap(true);
        grid->addWidget(cl, r, 1);
    }
    auto *change = new QPushButton(tr("Change Class…"), box);
    change->setToolTip(tr("Link %1 to a different class").arg(m_doc->fileName()));
    connect(change, &QPushButton::clicked, this, &Inspector::changeClassRequested);
    grid->addWidget(change, r++, 2);
    if (const ClassInfo *c = m_reg->get(actual); c && !c->scriptPath.isEmpty()) {
        auto *sp = new QLabel(c->scriptPath, box);
        sp->setObjectName(QStringLiteral("dimLabel"));
        sp->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(sp, r++, 1, 1, 2);
        if (!c->doc.isEmpty()) {
            auto *d = new QLabel(c->doc, box);
            d->setWordWrap(true);
            d->setObjectName(QStringLiteral("dimLabel"));
            grid->addWidget(d, r++, 1, 1, 2);
        }
    }
    grid->setColumnStretch(1, 1);
    into->addWidget(box);

    if (!it.value.isObject()) {
        auto *err = new QLabel(tr("This item is a %1, but items must be JSON objects.").arg(it.value.typeName()), parent);
        err->setProperty("state", "error");
        into->addWidget(err);
        auto *fix = new QPushButton(tr("Replace with an empty %1").arg(m_schema->displayName(declared)), parent);
        connect(fix, &QPushButton::clicked, this, [this, declared] {
            commitStructural({}, m_schema->newObject(declared, declared), tr("Replace item"));
        });
        into->addWidget(fix);
    }
}

void Inspector::buildFields(QVBoxLayout *into, const JPath &base, const JValue &obj, const QString &classKey,
                            const QString &declaredKey, int depth, bool root)
{
    QWidget *parent = into->parentWidget();
    const QList<ResolvedProperty> &props = m_reg->properties(classKey);
    const QString keyField = root ? m_doc->link().keyField : QString();
    const QList<const ClassInfo *> chain = m_reg->chain(classKey);

    int nonEmpty = 0;
    for (const ClassInfo *c : chain)
        for (const ResolvedProperty &p : props)
            if (p.ownerKey == c->key && p.info.name != keyField) {
                ++nonEmpty;
                break;
            }

    for (const ClassInfo *cls : chain) {
        QList<const ResolvedProperty *> own;
        for (const ResolvedProperty &p : props)
            if (p.ownerKey == cls->key && p.info.name != keyField)
                own << &p;
        if (own.isEmpty())
            continue;

        QVBoxLayout *target = into;
        if (root || nonEmpty > 1) {
            auto *cat = new CollapsibleSection(m_schema->displayName(cls->key), CollapsibleSection::Category, parent);
            if (cls->builtin)
                cat->setSubtitle(tr("built-in"));
            else if (!cls->scriptPath.isEmpty())
                cat->setSubtitle(cls->scriptPath.section(QLatin1Char('/'), -1));
            trackSection(cat, pathPattern(base) + QStringLiteral("#cat:") + cls->key, !cls->builtin);
            into->addWidget(cat);
            target = cat->bodyLayout();
        }

        QString curGroup, curSub;
        QVBoxLayout *groupLayout = target;
        QVBoxLayout *subLayout = target;
        for (const ResolvedProperty *rp : own) {
            if (rp->info.group != curGroup) {
                curGroup = rp->info.group;
                curSub.clear();
                if (curGroup.isEmpty()) {
                    groupLayout = target;
                } else {
                    auto *g = new CollapsibleSection(curGroup, CollapsibleSection::Group, parent);
                    trackSection(g, pathPattern(base) + QStringLiteral("#grp:") + cls->key + QLatin1Char('/') + curGroup, true);
                    target->addWidget(g);
                    groupLayout = g->bodyLayout();
                }
                subLayout = groupLayout;
            }
            if (rp->info.subgroup != curSub) {
                curSub = rp->info.subgroup;
                if (curSub.isEmpty()) {
                    subLayout = groupLayout;
                } else {
                    auto *g = new CollapsibleSection(curSub, CollapsibleSection::Group, parent);
                    trackSection(g, pathPattern(base) + QStringLiteral("#sub:") + cls->key + QLatin1Char('/') + curGroup
                                        + QLatin1Char('/') + curSub, true);
                    groupLayout->addWidget(g);
                    subLayout = g->bodyLayout();
                }
            }
            subLayout->addWidget(buildProperty(base + PathSeg::k(rp->info.name), *rp, depth));
        }
    }
    buildUnknownKeys(into, base, obj, classKey, declaredKey, root);
}

void Inspector::buildUnknownKeys(QVBoxLayout *into, const JPath &base, const JValue &obj, const QString &classKey,
                                 const QString &declaredKey, bool root)
{
    Schema::Poly poly;
    const bool hasPoly = m_schema->polyFor(declaredKey, &poly);
    QStringList unknown;
    for (const JMember &m : obj.members()) {
        if (hasPoly && m.key == poly.key)
            continue;
        if (root && m.key == m_doc->link().keyField)
            continue;
        if (!m_reg->property(classKey, m.key))
            unknown << m.key;
    }
    if (root && !m_doc->link().keyField.isEmpty() && obj.contains(m_doc->link().keyField))
        unknown.prepend(m_doc->link().keyField);
    if (unknown.isEmpty())
        return;

    auto *box = new QFrame(into->parentWidget());
    box->setObjectName(QStringLiteral("unknownBox"));
    auto *l = new QVBoxLayout(box);
    l->setContentsMargins(12, 10, 12, 10);
    l->setSpacing(Theme::RowSpacing);
    auto *head = new QHBoxLayout();
    auto *title = new QLabel(tr("Fields not declared in %1").arg(m_schema->displayName(classKey)), box);
    title->setProperty("state", "error");
    title->setToolTip(tr("These keys are not variables of the class, so Godot would ignore them or fail.\n"
                         "Remove them, or add the variable to the script and reload."));
    head->addWidget(title, 1);
    auto *all = new QPushButton(tr("Remove all"), box);
    connect(all, &QPushButton::clicked, this, [this, base, unknown] {
        JValue o = stored(base);
        for (const QString &k : unknown)
            o.remove(k);
        commitStructural(base, o, tr("Remove unknown fields"));
    });
    head->addWidget(all);
    l->addLayout(head);
    for (const QString &k : unknown) {
        auto *row = new QHBoxLayout();
        auto *kl = new QLabel(QStringLiteral("<b>%1</b>").arg(k.toHtmlEscaped()), box);
        kl->setFixedWidth(labelWidth(0));
        row->addWidget(kl);
        auto *vl = new QLabel(obj.value(k).preview(60).toHtmlEscaped(), box);
        vl->setObjectName(QStringLiteral("dimLabel"));
        row->addWidget(vl, 1);
        auto *rm = makeToolButton(QStringLiteral("✕"), tr("Remove \"%1\" from the JSON").arg(k), box);
        connect(rm, &QToolButton::clicked, this, [this, base, k] {
            commitStructural(base + PathSeg::k(k), JValue(), tr("Remove %1").arg(k));
        });
        row->addWidget(rm);
        l->addLayout(row);
        Row r;
        r.path = base + PathSeg::k(k);
        r.label = kl;
        r.revertable = false;
        r.anchor = kl;
        m_rows << r;
    }
    into->addWidget(box);
}

QWidget *Inspector::buildProperty(const JPath &path, const ResolvedProperty &rp, int depth)
{
    Row row;
    row.path = path;
    QStringList tip;
    tip << QStringLiteral("<b>%1</b>: %2").arg(rp.info.name.toHtmlEscaped(), rp.type.displayName().toHtmlEscaped());
    if (!rp.info.doc.isEmpty())
        tip << rp.info.doc.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    const QString h = hintText(rp.info.hint);
    if (!h.isEmpty())
        tip << h.toHtmlEscaped();
    tip << tr("Default: %1").arg(rp.defaultValue.preview(60).toHtmlEscaped());
    if (rp.type.kind == VKind::Variant && !rp.info.typeRaw.isEmpty())
        tip << tr("Type %1 is unknown: any value is accepted.").arg(rp.info.typeRaw.toHtmlEscaped());
    row.tooltip = tip.join(QStringLiteral("<br>"));
    m_defaults.insert(pathToString(path), rp.defaultValue);
    QWidget *w = buildValue(path, prettify(rp.info.name), rp.type, rp.info.hint, rp.defaultValue, depth, row, false);
    m_rows << row;
    return w;
}

QWidget *Inspector::buildValue(const JPath &path, const QString &label, const TypeRef &t, const PropertyHint &h,
                               const JValue &def, int depth, Row &row, bool inArray)
{
    QWidget *parent = m_content;
    if (isComplex(t, m_reg)) {
        CollapsibleSection *sec = nullptr;
        if (t.kind == VKind::Array)
            sec = arraySection(path, label, t, h, depth, row);
        else if (t.kind == VKind::Dictionary)
            sec = dictSection(path, label, t, depth, row);
        else
            sec = objectSection(path, label, t, depth, row, inArray);
        row.label = sec->titleLabel();
        row.anchor = sec;
        row.section = sec;
        sec->titleLabel()->setFixedWidth(labelWidth(depth) - 20);
        if (!inArray) {
            auto *rev = makeToolButton(QStringLiteral("↺"), tr("Remove from the JSON so the class default is used"), sec);
            connect(rev, &QToolButton::clicked, this, [this, path, label] {
                commitStructural(path, JValue(), tr("Revert %1").arg(label));
            });
            sec->headerLayout()->addWidget(rev);
            row.revert = rev;
        } else {
            row.revertable = false;
        }
        return sec;
    }

    const JValue st = stored(path);
    const JValue eff = st.isUndefined() ? def : st;
    auto *line = new QWidget(parent);
    auto *hl = new QHBoxLayout(line);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::FieldSpacing);
    if (!label.isEmpty()) {
        auto *lab = makeLabel(label, depth, line);
        hl->addWidget(lab);
        row.label = lab;
    }
    QWidget *ed = simpleEditor(path, t, h, eff, def);
    ed->setParent(line);
    hl->addWidget(ed, 1);
    if (!inArray) {
        auto *rev = makeToolButton(QStringLiteral("↺"), tr("Remove from the JSON so the class default is used"), line);
        connect(rev, &QToolButton::clicked, this, [this, path, label] {
            commitStructural(path, JValue(), tr("Revert %1").arg(label));
        });
        hl->addWidget(rev);
        row.revert = rev;
    } else {
        row.revertable = false;
    }
    row.anchor = line;
    return line;
}

QWidget *Inspector::simpleEditor(const JPath &path, const TypeRef &t, const PropertyHint &h, const JValue &eff,
                                 const JValue &def)
{
    Q_UNUSED(def);
    QWidget *parent = m_content;
    switch (t.kind) {
    case VKind::Bool: {
        auto *cb = new QCheckBox(tr("On"), parent);
        cb->setChecked(eff.toBool());
        connect(cb, &QCheckBox::toggled, this, [this, path](bool v) { commit(path, JValue(v), tr("Toggle"), false); });
        return cb;
    }
    case VKind::Int:
        return intEditor(path, t, h, eff);
    case VKind::Float:
        return floatEditor(path, h, eff);
    case VKind::String:
    case VKind::StringName:
    case VKind::NodePath:
        return stringEditor(path, t, h, eff);
    case VKind::Color:
        return colorEditor(path, h, eff);
    case VKind::Object: {
        const auto mode = m_reg->objectMode(t.className);
        if (mode == ClassRegistry::ObjectMode::ResourcePath)
            return pathEditor(path, eff, resourceFilters(t.className), false, false, true);
        if (mode == ClassRegistry::ObjectMode::NodeRef) {
            PropertyHint none;
            TypeRef np;
            np.kind = VKind::NodePath;
            return stringEditor(path, np, none, eff.isNull() ? JValue(QString()) : eff);
        }
        return variantEditor(path, eff);
    }
    case VKind::Variant:
        return variantEditor(path, eff);
    default:
        if (t.isVector())
            return vectorEditor(path, t, eff);
        return variantEditor(path, eff);
    }
}

QWidget *Inspector::intEditor(const JPath &path, const TypeRef &t, const PropertyHint &h, const JValue &eff)
{
    QWidget *parent = m_content;
    // Enum: from a typed enum or @export_enum
    QList<QPair<QString, qint64>> options;
    if (const EnumInfo *e = t.enumKey.isEmpty() ? nullptr : m_reg->enumInfo(t.enumKey))
        options = e->values;
    else if (h.kind == PropertyHint::Enum)
        options = h.options;

    if (!options.isEmpty()) {
        auto *combo = new QComboBox(parent);
        WheelGuard::install(combo);
        const bool asString = eff.isString();
        qint64 cur = eff.toInt();
        if (asString)
            for (const auto &o : options)
                if (o.first == eff.toString())
                    cur = o.second;
        int sel = -1;
        for (const auto &o : options) {
            combo->addItem(QStringLiteral("%1  (%2)").arg(o.first).arg(o.second), QVariant::fromValue(o.second));
            if (o.second == cur && (!asString || o.first == eff.toString()))
                sel = combo->count() - 1;
        }
        if (sel < 0) {
            combo->insertItem(0, tr("⚠ invalid: %1").arg(eff.preview()), QVariant());
            sel = 0;
        }
        combo->setCurrentIndex(sel);
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo, path, asString, options](int i) {
            const QVariant d = combo->itemData(i);
            if (!d.isValid())
                return;
            const qint64 v = d.toLongLong();
            JValue out(v);
            if (asString)
                for (const auto &o : options)
                    if (o.second == v)
                        out = JValue(o.first);
            commit(path, out, tr("Change value"), false);
        });
        return combo;
    }

    if (h.kind == PropertyHint::Flags) {
        auto *w = new QWidget(parent);
        auto *grid = new QGridLayout(w);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(6);
        const int cols = h.options.size() > 8 ? 4 : 2;
        const qint64 cur = eff.toInt();
        auto boxes = std::make_shared<QList<QPair<QCheckBox *, qint64>>>();
        for (int i = 0; i < h.options.size(); ++i) {
            auto *cb = new QCheckBox(h.options[i].first, w);
            cb->setChecked((cur & h.options[i].second) == h.options[i].second && h.options[i].second != 0);
            grid->addWidget(cb, i / cols, i % cols);
            boxes->append(qMakePair(cb, h.options[i].second));
            connect(cb, &QCheckBox::toggled, this, [this, path, boxes] {
                qint64 v = 0;
                for (const auto &b : *boxes)
                    if (b.first->isChecked())
                        v |= b.second;
                commit(path, JValue(v), tr("Change flags"), false);
            });
        }
        return w;
    }

    double min = std::numeric_limits<int>::min(), max = std::numeric_limits<int>::max();
    if (h.kind == PropertyHint::Range) {
        if (h.hasMin && !h.orLess) min = h.min;
        if (h.hasMax && !h.orGreater) max = h.max;
    }
    const bool slider = h.kind == PropertyHint::Range && !h.hideSlider && !h.orGreater && !h.orLess;
    auto *ne = new NumberEdit(true, min, max, h.step, slider, h.suffix, parent);
    ne->setValue(double(eff.toInt()));
    connect(ne, &NumberEdit::valueCommitted, this, [this, path](double v, bool) {
        commit(path, JValue(qint64(std::llround(v))), tr("Change value"));
    });
    return ne;
}

QWidget *Inspector::floatEditor(const JPath &path, const PropertyHint &h, const JValue &eff)
{
    double min = -1e300, max = 1e300;
    if (h.kind == PropertyHint::Range) {
        if (h.hasMin && !h.orLess) min = h.min;
        if (h.hasMax && !h.orGreater) max = h.max;
    }
    const bool slider = h.kind == PropertyHint::Range && !h.hideSlider && !h.orGreater && !h.orLess;
    double step = h.step;
    if (h.kind == PropertyHint::Range && step <= 0)
        step = 0.001;
    auto *ne = new NumberEdit(false, min, max, step, slider, h.suffix, m_content);
    ne->setValue(eff.toDouble());
    connect(ne, &NumberEdit::valueCommitted, this, [this, path](double v, bool) {
        commit(path, JValue(v, false), tr("Change value"));
    });
    return ne;
}

QWidget *Inspector::stringEditor(const JPath &path, const TypeRef &t, const PropertyHint &h, const JValue &eff)
{
    QWidget *parent = m_content;
    if (h.kind == PropertyHint::Enum) {
        auto *combo = new QComboBox(parent);
        WheelGuard::install(combo);
        int sel = -1;
        for (const auto &o : h.options) {
            combo->addItem(o.first, o.first);
            if (o.first == eff.toString())
                sel = combo->count() - 1;
        }
        if (sel < 0) {
            combo->insertItem(0, tr("⚠ invalid: %1").arg(eff.preview()), QVariant());
            sel = 0;
        }
        combo->setCurrentIndex(sel);
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo, path](int i) {
            const QVariant d = combo->itemData(i);
            if (d.isValid())
                commit(path, JValue(d.toString()), tr("Change value"), false);
        });
        return combo;
    }
    if (h.kind == PropertyHint::File || h.kind == PropertyHint::Dir)
        return pathEditor(path, eff, h.filters, h.kind == PropertyHint::Dir, h.globalPath, false);
    if (h.kind == PropertyHint::Multiline) {
        auto *te = new CommitPlainTextEdit(parent);
        te->setPlainText(eff.toString());
        te->setFixedHeight(80);
        te->setTabChangesFocus(true);
        connect(te, &CommitPlainTextEdit::editingFinished, this, [this, te, path] {
            commit(path, JValue(te->toPlainText()), tr("Edit text"), false);
        });
        return te;
    }
    auto *le = new QLineEdit(eff.toString(), parent);
    if (h.kind == PropertyHint::Placeholder)
        le->setPlaceholderText(h.placeholder);
    else if (t.kind == VKind::NodePath)
        le->setPlaceholderText(QStringLiteral("NodePath"));
    connect(le, &QLineEdit::editingFinished, this, [this, le, path] {
        commit(path, JValue(le->text()), tr("Edit text"), false);
    });
    return le;
}

QWidget *Inspector::vectorEditor(const JPath &path, const TypeRef &t, const JValue &eff)
{
    auto *w = new QWidget(m_content);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::FieldSpacing);
    QVector<double> vals;
    if (!Values::readVector(eff, t, &vals))
        vals = QVector<double>(t.components(), 0.0);
    const QStringList names = Values::componentNames(t);
    auto spins = std::make_shared<QList<QAbstractSpinBox *>>();
    auto emitAll = [this, path, t, spins] {
        QVector<double> out;
        for (QAbstractSpinBox *s : *spins) {
            if (auto *i = qobject_cast<QSpinBox *>(s)) out << i->value();
            else if (auto *d = qobject_cast<QDoubleSpinBox *>(s)) out << d->value();
        }
        const JValue like = stored(path);
        const JValue def = m_defaults.value(pathToString(path));
        commit(path, Values::writeVector(out, t, m_schema->format(), like.isUndefined() ? def : like),
               tr("Change %1").arg(t.displayName()));
    };
    for (int i = 0; i < names.size(); ++i) {
        auto *l = new QLabel(names[i], w);
        l->setObjectName(QStringLiteral("dimLabel"));
        hl->addWidget(l);
        if (t.isIntVector()) {
            auto *sb = new QSpinBox(w);
            sb->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            sb->setValue(int(vals.value(i)));
            sb->setKeyboardTracking(false);
            WheelGuard::install(sb);
            connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, emitAll);
            spins->append(sb);
            hl->addWidget(sb, 1);
        } else {
            auto *sb = new SmartDoubleSpinBox(w);
            sb->setLocale(QLocale::c());
            sb->setDecimals(6);
            sb->setRange(-1e12, 1e12);
            sb->setValue(vals.value(i));
            sb->setKeyboardTracking(false);
            sb->setMinimumWidth(50);
            WheelGuard::install(sb);
            connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, emitAll);
            spins->append(sb);
            hl->addWidget(sb, 1);
        }
    }
    return w;
}

QWidget *Inspector::colorEditor(const JPath &path, const PropertyHint &h, const JValue &eff)
{
    auto *w = new QWidget(m_content);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::FieldSpacing);
    QColor c(0, 0, 0);
    Values::readColor(eff, &c);
    const bool alpha = h.kind != PropertyHint::ColorNoAlpha;
    auto *swatch = new QPushButton(w);
    swatch->setFixedSize(64, 32);
    auto paint = [swatch](const QColor &col) {
        swatch->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid palette(mid); border-radius: 3px; }")
                                  .arg(col.name(QColor::HexArgb)));
    };
    paint(c);
    auto *hex = new QLineEdit(w);
    hex->setText(eff.isString() ? eff.toString() : JsonIO::writeCompact(eff));
    hex->setToolTip(tr("Hex (#rrggbb or #rrggbbaa)"));
    auto apply = [this, path, alpha](const QColor &col) {
        const JValue like = stored(path);
        const JValue def = m_defaults.value(pathToString(path));
        commit(path, Values::writeColor(col, m_schema->format(), alpha, like.isUndefined() ? def : like),
               tr("Change color"), false);
    };
    connect(swatch, &QPushButton::clicked, this, [this, swatch, hex, paint, apply, path, alpha] {
        QColor cur(0, 0, 0);
        const JValue st = stored(path);
        Values::readColor(st.isUndefined() ? m_defaults.value(pathToString(path)) : st, &cur);
        const QColor col = QColorDialog::getColor(cur, swatch, tr("Pick a color"),
                                                  alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
        if (!col.isValid())
            return;
        paint(col);
        apply(col);
        const JValue nv = stored(path);
        hex->setText(nv.isString() ? nv.toString() : JsonIO::writeCompact(nv));
    });
    connect(hex, &QLineEdit::editingFinished, this, [this, hex, paint, apply, path] {
        QColor col;
        if (!Values::readColor(JValue(hex->text().trimmed()), &col)) {
            hex->setProperty("invalid", true);
            repolish(hex);
            emit statusMessage(tr("\"%1\" is not a valid color.").arg(hex->text()));
            return;
        }
        hex->setProperty("invalid", false);
        repolish(hex);
        paint(col);
        apply(col);
        Q_UNUSED(path);
    });
    hl->addWidget(swatch);
    hl->addWidget(hex, 1);
    return w;
}

QStringList Inspector::resourceFilters(const QString &classKey) const
{
    auto is = [&](const char *base) { return m_reg->inherits(classKey, QLatin1String(base)); };
    if (is("Texture") || is("Image"))
        return {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.webp"),
                QStringLiteral("*.svg"), QStringLiteral("*.bmp"), QStringLiteral("*.tga"), QStringLiteral("*.exr"),
                QStringLiteral("*.hdr"), QStringLiteral("*.tres"), QStringLiteral("*.res")};
    if (is("AudioStream"))
        return {QStringLiteral("*.wav"), QStringLiteral("*.ogg"), QStringLiteral("*.mp3"), QStringLiteral("*.tres")};
    if (is("PackedScene"))
        return {QStringLiteral("*.tscn"), QStringLiteral("*.scn")};
    if (is("Font"))
        return {QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.woff"), QStringLiteral("*.woff2"),
                QStringLiteral("*.fnt"), QStringLiteral("*.tres")};
    if (is("Script"))
        return {QStringLiteral("*.gd"), QStringLiteral("*.cs")};
    if (is("Shader"))
        return {QStringLiteral("*.gdshader"), QStringLiteral("*.tres")};
    if (is("Mesh"))
        return {QStringLiteral("*.obj"), QStringLiteral("*.glb"), QStringLiteral("*.gltf"), QStringLiteral("*.tres"),
                QStringLiteral("*.res")};
    return {QStringLiteral("*.tres"), QStringLiteral("*.res")};
}

QWidget *Inspector::pathEditor(const JPath &path, const JValue &eff, const QStringList &filters, bool dirMode,
                               bool allowGlobal, bool nullable)
{
    auto *w = new QWidget(m_content);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::FieldSpacing);
    auto *thumb = new QLabel(w);
    thumb->setFixedSize(32, 32);
    thumb->setScaledContents(false);
    hl->addWidget(thumb);
    auto *le = new QLineEdit(eff.toString(), w);
    le->setPlaceholderText(nullable ? tr("<empty>") : (dirMode ? QStringLiteral("res://folder") : QStringLiteral("res://file")));
    if (!filters.isEmpty())
        le->setToolTip(filters.join(QLatin1Char(' ')));
    hl->addWidget(le, 1);
    auto updateThumb = [this, thumb](const QString &res) {
        const QString abs = m_reg->toAbs(res);
        if (imageExts().contains(QFileInfo(res).suffix().toLower()) && QFileInfo::exists(abs)) {
            QPixmap pm(abs);
            thumb->setPixmap(pm.scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation));
            thumb->setToolTip(res);
        } else {
            thumb->clear();
        }
    };
    updateThumb(eff.toString());

    auto validate = [this, filters, dirMode, allowGlobal](const QString &s, QString *err) {
        if (s.isEmpty())
            return true;
        if (!allowGlobal && !s.startsWith(QLatin1String("res://")) && !s.startsWith(QLatin1String("uid://"))) {
            *err = tr("The path must start with res://");
            return false;
        }
        if (!dirMode && !Validator::matchesFileFilter(s, filters)) {
            *err = tr("\"%1\" does not match %2").arg(s, filters.join(QLatin1Char(' ')));
            return false;
        }
        return true;
    };
    auto setValue = [this, path, nullable, le, updateThumb](const QString &s) {
        le->setProperty("invalid", false);
        repolish(le);
        updateThumb(s);
        commit(path, s.isEmpty() && nullable ? JValue::null() : JValue(s), tr("Change path"), false);
    };
    connect(le, &QLineEdit::editingFinished, this, [this, le, validate, setValue] {
        QString err;
        const QString s = le->text().trimmed();
        if (!validate(s, &err)) {
            le->setProperty("invalid", true);
            repolish(le);
            emit statusMessage(err);
            return;
        }
        setValue(s);
    });
    auto *browse = makeToolButton(QStringLiteral("…"), dirMode ? tr("Choose a folder") : tr("Choose a file"), w);
    connect(browse, &QToolButton::clicked, this, [this, le, filters, dirMode, allowGlobal, validate, setValue] {
        const QString root = m_project->root();
        QString start = root;
        const QString cur = le->text().trimmed();
        if (cur.startsWith(QLatin1String("res://")))
            start = QFileInfo(m_reg->toAbs(cur)).absolutePath();
        QString chosen;
        if (dirMode) {
            chosen = QFileDialog::getExistingDirectory(this, tr("Choose a folder"), start);
        } else {
            QString filter = filters.isEmpty() ? tr("All files (*)")
                                               : tr("Allowed files (%1);;All files (*)").arg(filters.join(QLatin1Char(' ')));
            chosen = QFileDialog::getOpenFileName(this, tr("Choose a file"), start, filter);
        }
        if (chosen.isEmpty())
            return;
        QString res = m_reg->toRes(chosen);
        if (!res.startsWith(QLatin1String("res://"))) {
            if (!allowGlobal) {
                emit statusMessage(tr("The file must be inside the Godot project."));
                return;
            }
            res = chosen;
        }
        QString err;
        if (!validate(res, &err)) {
            emit statusMessage(err);
            return;
        }
        le->setText(res);
        setValue(res);
    });
    hl->addWidget(browse);
    if (nullable) {
        auto *clear = makeToolButton(QStringLiteral("✕"), tr("Clear (null)"), w);
        connect(clear, &QToolButton::clicked, this, [le, setValue] {
            le->clear();
            setValue(QString());
        });
        hl->addWidget(clear);
    }
    return w;
}

QWidget *Inspector::variantEditor(const JPath &path, const JValue &eff)
{
    auto *w = new QWidget(m_content);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::FieldSpacing);
    auto *type = new QComboBox(w);
    WheelGuard::install(type);
    const QStringList types = {QStringLiteral("null"), QStringLiteral("bool"), QStringLiteral("int"),
                               QStringLiteral("float"), QStringLiteral("String"), QStringLiteral("JSON")};
    type->addItems(types);
    int cur = 0;
    switch (eff.type()) {
    case JValue::Bool: cur = 1; break;
    case JValue::Number: cur = eff.hasIntLexeme() ? 2 : 3; break;
    case JValue::String: cur = 4; break;
    case JValue::Array: case JValue::Object: cur = 5; break;
    default: cur = 0;
    }
    type->setCurrentIndex(cur);
    type->setFixedWidth(80);
    hl->addWidget(type);
    connect(type, qOverload<int>(&QComboBox::activated), this, [this, path](int i) {
        JValue v;
        switch (i) {
        case 1: v = JValue(false); break;
        case 2: v = JValue(0); break;
        case 3: v = JValue(0.0, false); break;
        case 4: v = JValue(QString()); break;
        case 5: v = JValue::object(); break;
        default: v = JValue::null();
        }
        commitStructural(path, v, tr("Change type"));
    });
    switch (cur) {
    case 1: {
        auto *cb = new QCheckBox(tr("On"), w);
        cb->setChecked(eff.toBool());
        connect(cb, &QCheckBox::toggled, this, [this, path](bool v) { commit(path, JValue(v), tr("Toggle"), false); });
        hl->addWidget(cb, 1);
        break;
    }
    case 2:
    case 3: {
        PropertyHint none;
        hl->addWidget(cur == 2 ? intEditor(path, TypeRef(), none, eff) : floatEditor(path, none, eff), 1);
        break;
    }
    case 4: {
        auto *le = new QLineEdit(eff.toString(), w);
        connect(le, &QLineEdit::editingFinished, this, [this, le, path] {
            commit(path, JValue(le->text()), tr("Edit text"), false);
        });
        hl->addWidget(le, 1);
        break;
    }
    case 5: {
        auto *le = new QLineEdit(JsonIO::writeCompact(eff), w);
        le->setToolTip(tr("Any JSON value"));
        connect(le, &QLineEdit::editingFinished, this, [this, le, path] {
            JsonParseError err;
            const JValue v = JsonIO::parse(le->text(), &err);
            if (!err.ok()) {
                le->setProperty("invalid", true);
                repolish(le);
                emit statusMessage(tr("Invalid JSON: %1").arg(err.message));
                return;
            }
            le->setProperty("invalid", false);
            repolish(le);
            commit(path, v, tr("Edit value"), false);
        });
        hl->addWidget(le, 1);
        break;
    }
    default:
        hl->addStretch(1);
    }
    return w;
}

JValue Inspector::newElement(const TypeRef &elem, const QString &classKey) const
{
    if (elem.kind == VKind::Object && m_reg->objectMode(elem.className) == ClassRegistry::ObjectMode::Inline)
        return m_schema->newObject(classKey.isEmpty() ? elem.className : classKey, elem.className);
    JValue v = Values::typeDefault(elem, m_schema->format());
    if (elem.kind == VKind::Int) {
        if (const EnumInfo *e = m_reg->enumInfo(elem.enumKey); e && !e->values.isEmpty())
            v = JValue(e->values.first().second);
    }
    return v;
}

CollapsibleSection *Inspector::arraySection(const JPath &path, const QString &label, const TypeRef &t,
                                            const PropertyHint &h, int depth, Row &row)
{
    Q_UNUSED(row);
    const JValue st = stored(path);
    const JValue def = m_defaults.value(pathToString(path));
    const JValue arr = st.isUndefined() ? (def.isArray() ? def : JValue::array()) : st;
    auto *sec = new CollapsibleSection(label, CollapsibleSection::Value, m_content);
    const TypeRef elem = t.elem ? *t.elem : TypeRef();
    trackSection(sec, pathPattern(path), depth < 2);

    if (!arr.isArray()) {
        sec->setSubtitle(tr("⚠ not an array"));
        auto *fix = new QPushButton(tr("Replace with an empty array"), sec);
        connect(fix, &QPushButton::clicked, this, [this, path] {
            commitStructural(path, JValue::array(), tr("Reset array"));
        });
        sec->bodyLayout()->addWidget(fix);
        return sec;
    }
    sec->setSubtitle(QStringLiteral("%1 (%2)").arg(t.displayName()).arg(arr.size()));

    auto *add = makeToolButton(QStringLiteral("+"), tr("Add an element"), sec);
    const bool inlineObj = elem.kind == VKind::Object && m_reg->objectMode(elem.className) == ClassRegistry::ObjectMode::Inline;
    const QStringList classes = inlineObj ? m_schema->assignableClasses(elem.className) : QStringList();
    auto addWith = [this, path, elem, sec](const QString &cls) {
        JValue a = stored(path);
        if (!a.isArray()) {
            const JValue d = m_defaults.value(pathToString(path));
            a = d.isArray() ? d : JValue::array();
        }
        a.append(newElement(elem, cls));
        sec->setExpanded(true);
        m_expanded.insert(sec->property("stateKey").toString(), true);
        commitStructural(path, a, tr("Add element"));
    };
    if (classes.size() > 1) {
        auto *menu = new QMenu(add);
        for (const QString &c : classes)
            menu->addAction(m_schema->displayName(c), this, [addWith, c] { addWith(c); });
        add->setMenu(menu);
        add->setPopupMode(QToolButton::InstantPopup);
        add->setFixedWidth(30);
    } else {
        connect(add, &QToolButton::clicked, this, [addWith] { addWith(QString()); });
    }
    sec->headerLayout()->addWidget(add);

    for (int i = 0; i < arr.size(); ++i) {
        const JPath ep = path + PathSeg::i(i);
        m_defaults.insert(pathToString(ep), arr.at(i));
        auto makeButtons = [this, path, i, &arr](QWidget *parent, QHBoxLayout *into) {
            const int n = arr.size();
            auto *up = makeToolButton(QStringLiteral("▲"), tr("Move up"), parent);
            up->setEnabled(i > 0);
            auto *down = makeToolButton(QStringLiteral("▼"), tr("Move down"), parent);
            down->setEnabled(i < n - 1);
            auto *dup = makeToolButton(QStringLiteral("⧉"), tr("Duplicate"), parent);
            auto *rm = makeToolButton(QStringLiteral("✕"), tr("Remove"), parent);
            auto edit = [this, path](std::function<void(JValue &)> fn, const QString &text) {
                JValue a = stored(path);
                if (!a.isArray())
                    a = m_defaults.value(pathToString(path));
                fn(a);
                commitStructural(path, a, text);
            };
            connect(up, &QToolButton::clicked, this, [edit, i] {
                edit([i](JValue &a) { std::swap(a.elements()[size_t(i)], a.elements()[size_t(i - 1)]); }, tr("Move element"));
            });
            connect(down, &QToolButton::clicked, this, [edit, i] {
                edit([i](JValue &a) { std::swap(a.elements()[size_t(i)], a.elements()[size_t(i + 1)]); }, tr("Move element"));
            });
            connect(dup, &QToolButton::clicked, this, [edit, i] {
                edit([i](JValue &a) { a.insert(i + 1, a.at(i)); }, tr("Duplicate element"));
            });
            connect(rm, &QToolButton::clicked, this, [edit, i] {
                edit([i](JValue &a) { a.removeAt(i); }, tr("Remove element"));
            });
            into->addWidget(up);
            into->addWidget(down);
            into->addWidget(dup);
            into->addWidget(rm);
        };

        Row er;
        er.path = ep;
        er.revertable = false;
        if (isComplex(elem, m_reg)) {
            QWidget *w = buildValue(ep, QStringLiteral("[%1]").arg(i), elem, h, arr.at(i), depth + 1, er, true);
            if (auto *s = qobject_cast<CollapsibleSection *>(w))
                makeButtons(s, s->headerLayout());
            sec->bodyLayout()->addWidget(w);
        } else {
            auto *line = new QWidget(sec);
            auto *hl = new QHBoxLayout(line);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(Theme::FieldSpacing);
            auto *idx = new QLabel(QString::number(i), line);
            idx->setObjectName(QStringLiteral("dimLabel"));
            idx->setFixedWidth(34);
            hl->addWidget(idx);
            QWidget *ed = simpleEditor(ep, elem, h, arr.at(i), arr.at(i));
            ed->setParent(line);
            hl->addWidget(ed, 1);
            makeButtons(line, hl);
            er.label = idx;
            er.anchor = line;
            sec->bodyLayout()->addWidget(line);
        }
        m_rows << er;
    }
    if (arr.size() == 0) {
        auto *empty = new QLabel(tr("Empty"), sec);
        empty->setObjectName(QStringLiteral("dimLabel"));
        sec->bodyLayout()->addWidget(empty);
    }
    return sec;
}

CollapsibleSection *Inspector::dictSection(const JPath &path, const QString &label, const TypeRef &t, int depth,
                                           Row &row)
{
    Q_UNUSED(row);
    const JValue st = stored(path);
    const JValue def = m_defaults.value(pathToString(path));
    const JValue dict = st.isUndefined() ? (def.isObject() ? def : JValue::object()) : st;
    auto *sec = new CollapsibleSection(label, CollapsibleSection::Value, m_content);
    trackSection(sec, pathPattern(path), depth < 2);
    if (!dict.isObject()) {
        sec->setSubtitle(tr("⚠ not an object"));
        auto *fix = new QPushButton(tr("Replace with an empty dictionary"), sec);
        connect(fix, &QPushButton::clicked, this, [this, path] {
            commitStructural(path, JValue::object(), tr("Reset dictionary"));
        });
        sec->bodyLayout()->addWidget(fix);
        return sec;
    }
    sec->setSubtitle(QStringLiteral("%1 (%2)").arg(t.displayName()).arg(dict.size()));
    const TypeRef valType = t.elem ? *t.elem : TypeRef();
    const bool intKeys = t.key && t.key->kind == VKind::Int;

    auto current = [this, path] {
        JValue d = stored(path);
        if (!d.isObject()) {
            const JValue df = m_defaults.value(pathToString(path));
            d = df.isObject() ? df : JValue::object();
        }
        return d;
    };
    auto *add = makeToolButton(QStringLiteral("+"), tr("Add a key"), sec);
    connect(add, &QToolButton::clicked, this, [this, path, current, valType, intKeys, sec] {
        JValue d = current();
        QString k = intKeys ? QStringLiteral("0") : QStringLiteral("key");
        for (int n = 1; d.contains(k); ++n)
            k = intKeys ? QString::number(n) : QStringLiteral("key_%1").arg(n + 1);
        d.set(k, newElement(valType, QString()));
        sec->setExpanded(true);
        m_expanded.insert(sec->property("stateKey").toString(), true);
        commitStructural(path, d, tr("Add key"));
    });
    sec->headerLayout()->addWidget(add);

    for (const JMember &m : dict.members()) {
        const JPath vp = path + PathSeg::k(m.key);
        m_defaults.insert(pathToString(vp), m.value);
        const QString key = m.key;
        auto *keyEdit = new QLineEdit(key);
        keyEdit->setFixedWidth(labelWidth(depth + 1));
        connect(keyEdit, &QLineEdit::editingFinished, this, [this, keyEdit, key, path, current, intKeys] {
            const QString nk = keyEdit->text().trimmed();
            if (nk == key)
                return;
            JValue d = current();
            QString err;
            bool ok = true;
            if (intKeys)
                nk.toLongLong(&ok);
            if (nk.isEmpty())
                err = tr("Keys cannot be empty.");
            else if (!ok)
                err = tr("Keys of this dictionary must be integers.");
            else if (d.contains(nk))
                err = tr("The key \"%1\" already exists.").arg(nk);
            if (!err.isEmpty()) {
                keyEdit->setText(key);
                emit statusMessage(err);
                return;
            }
            d.renameKey(key, nk);
            commitStructural(path, d, tr("Rename key"));
        });
        auto *rm = makeToolButton(QStringLiteral("✕"), tr("Remove key"));
        connect(rm, &QToolButton::clicked, this, [this, path, key, current] {
            JValue d = current();
            d.remove(key);
            commitStructural(path, d, tr("Remove key"));
        });
        Row er;
        er.path = vp;
        er.revertable = false;
        if (isComplex(valType, m_reg)) {
            QWidget *w = buildValue(vp, key, valType, PropertyHint(), m.value, depth + 1, er, true);
            if (auto *s = qobject_cast<CollapsibleSection *>(w)) {
                keyEdit->setParent(s);
                s->headerLayout()->insertWidget(0, keyEdit);
                s->titleLabel()->hide();
                rm->setParent(s);
                s->headerLayout()->addWidget(rm);
            }
            sec->bodyLayout()->addWidget(w);
        } else {
            auto *line = new QWidget(sec);
            auto *hl = new QHBoxLayout(line);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(Theme::FieldSpacing);
            keyEdit->setParent(line);
            hl->addWidget(keyEdit);
            QWidget *ed = simpleEditor(vp, valType, PropertyHint(), m.value, m.value);
            ed->setParent(line);
            hl->addWidget(ed, 1);
            rm->setParent(line);
            hl->addWidget(rm);
            er.anchor = line;
            sec->bodyLayout()->addWidget(line);
        }
        m_rows << er;
    }
    if (dict.size() == 0) {
        auto *empty = new QLabel(tr("Empty"), sec);
        empty->setObjectName(QStringLiteral("dimLabel"));
        sec->bodyLayout()->addWidget(empty);
    }
    return sec;
}

CollapsibleSection *Inspector::objectSection(const JPath &path, const QString &label, const TypeRef &t, int depth,
                                             Row &row, bool inArray)
{
    Q_UNUSED(row);
    const JValue st = stored(path);
    const JValue def = m_defaults.value(pathToString(path));
    const JValue eff = st.isUndefined() ? def : st;
    const QString declared = t.className;
    auto *sec = new CollapsibleSection(label, CollapsibleSection::Value, m_content);
    trackSection(sec, pathPattern(path), depth < 2);
    const QStringList classes = m_schema->assignableClasses(declared);

    auto makeNewButton = [this, sec, classes, declared, path](const QString &text) {
        auto *b = new QToolButton(sec);
        b->setText(text);
        b->setAutoRaise(false);
        b->setFocusPolicy(Qt::NoFocus);
        auto create = [this, declared, path, sec](const QString &cls) {
            m_expanded.insert(sec->property("stateKey").toString(), true);
            commitStructural(path, m_schema->newObject(cls, declared), tr("New %1").arg(m_schema->displayName(cls)));
        };
        if (classes.size() > 1) {
            auto *menu = new QMenu(b);
            for (const QString &c : classes)
                menu->addAction(m_schema->displayName(c), this, [create, c] { create(c); });
            b->setMenu(menu);
            b->setPopupMode(QToolButton::InstantPopup);
        } else {
            connect(b, &QToolButton::clicked, this, [create, declared] { create(declared); });
        }
        return b;
    };

    if (eff.isNull() || eff.isUndefined()) {
        sec->setSubtitle(tr("<empty %1>").arg(t.displayName()));
        sec->headerLayout()->addWidget(makeNewButton(tr("New")));
        sec->setExpanded(false);
        sec->body()->hide();
        return sec;
    }
    if (eff.isString()) {
        sec->setSubtitle(tr("external resource"));
        sec->bodyLayout()->addWidget(pathEditor(path, eff, {QStringLiteral("*.tres"), QStringLiteral("*.res")}, false, false, true));
        sec->headerLayout()->addWidget(makeNewButton(tr("Make inline")));
        return sec;
    }
    if (!eff.isObject()) {
        sec->setSubtitle(tr("⚠ expected a %1 object").arg(t.displayName()));
        sec->headerLayout()->addWidget(makeNewButton(tr("Replace")));
        return sec;
    }
    QString polyErr;
    const QString actual = m_schema->actualClass(eff, declared, &polyErr);
    if (classes.size() > 1) {
        auto *combo = new QComboBox(sec);
        WheelGuard::install(combo);
        for (const QString &c : classes)
            combo->addItem(m_schema->displayName(c), c);
        combo->setCurrentIndex(qMax(0, classes.indexOf(actual)));
        if (!polyErr.isEmpty()) {
            combo->insertItem(0, tr("⚠ unknown type"), QVariant());
            combo->setCurrentIndex(0);
            combo->setToolTip(polyErr);
        }
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo, path, declared](int i) {
            const QVariant d = combo->itemData(i);
            if (!d.isValid())
                return;
            const QString cls = d.toString();
            const JValue old = stored(path).isObject() ? stored(path) : m_defaults.value(pathToString(path));
            JValue nv = m_schema->newObject(cls, declared);
            for (const JMember &m : old.members())
                if (m_reg->property(cls, m.key))
                    nv.set(m.key, m.value);
            nv.setLayout(old.layout());
            commitStructural(path, nv, tr("Change type"));
        });
        sec->headerLayout()->addWidget(combo);
    } else {
        sec->setSubtitle(m_schema->displayName(actual));
    }
    if (!inArray) {
        auto *clear = makeToolButton(QStringLiteral("✕"), tr("Set to null"), sec);
        connect(clear, &QToolButton::clicked, this, [this, path] {
            commitStructural(path, JValue::null(), tr("Clear object"));
        });
        sec->headerLayout()->addWidget(clear);
    }
    buildFields(sec->bodyLayout(), path, eff, actual, declared, depth + 1, false);
    return sec;
}

void Inspector::refreshStates()
{
    m_issues.clear();
    if (!m_doc || m_item < 0 || m_item >= m_doc->count() || m_doc->isPlain())
        return; // plain JSON is never validated
    Validator val(*m_schema);
    const QList<Issue> issues =
        val.validateItem(m_doc->item(m_item).value, m_doc->link().classKey, m_item, m_doc->link().keyField);
    for (const Issue &i : issues)
        m_issues[pathToString(i.path)] << (i.error ? QString() : tr("Warning: ")) + i.message;
    QSet<QString> errorPaths;
    for (const Issue &i : issues)
        if (i.error)
            errorPaths.insert(pathToString(i.path));

    for (Row &r : m_rows) {
        const QString ps = pathToString(r.path);
        const bool present = !stored(r.path).isUndefined();
        if (r.revert)
            r.revert->setVisible(r.revertable && present);
        if (!r.label)
            continue;
        QString state;
        QStringList msgs = m_issues.value(ps);
        if (!msgs.isEmpty()) {
            state = errorPaths.contains(ps) ? QStringLiteral("error") : QStringLiteral("warning");
        } else {
            bool child = false;
            for (const QString &p : errorPaths)
                if (p.startsWith(ps + QLatin1Char('.')) || p.startsWith(ps + QLatin1Char('['))) {
                    child = true;
                    break;
                }
            if (child)
                state = QStringLiteral("child-error");
            else if (r.revertable && !present)
                state = QStringLiteral("absent");
        }
        if (r.label->property("state").toString() != state) {
            r.label->setProperty("state", state);
            repolish(r.label);
        }
        QString tip = r.tooltip;
        if (r.revertable && !present)
            tip += (tip.isEmpty() ? QString() : QStringLiteral("<br>")) + tr("<i>Not stored in the JSON: the default is used.</i>");
        for (const QString &m : msgs)
            tip += QStringLiteral("<br><span style='color:#e5534b'>%1</span>").arg(m.toHtmlEscaped());
        r.label->setToolTip(tip);
    }
}

void Inspector::reveal(const JPath &path)
{
    // Find the deepest row matching the path (or one of its ancestors).
    const Row *best = nullptr;
    int bestLen = -1;
    for (const Row &r : m_rows) {
        if (r.path.size() > path.size() || r.path.size() <= bestLen)
            continue;
        if (path.mid(0, r.path.size()) == r.path) {
            best = &r;
            bestLen = r.path.size();
        }
    }
    if (!best || !best->anchor)
        return;
    QWidget *anchor = best->anchor;
    for (QWidget *w = anchor->parentWidget(); w; w = w->parentWidget()) {
        if (auto *sec = qobject_cast<CollapsibleSection *>(w)) {
            sec->setExpanded(true);
            m_expanded.insert(sec->property("stateKey").toString(), true);
        }
    }
    if (auto *sec = qobject_cast<CollapsibleSection *>(anchor)) {
        sec->setExpanded(true);
        m_expanded.insert(sec->property("stateKey").toString(), true);
    }
    QTimer::singleShot(0, this, [this, anchor = QPointer<QWidget>(anchor)] {
        if (!anchor)
            return;
        ensureWidgetVisible(anchor, 20, 60);
        anchor->setFocus();
        if (QWidget *child = anchor->findChild<QLineEdit *>())
            child->setFocus();
    });
}

// ---------------------------------------------------------------- No class / plain JSON

void Inspector::buildUnlinked(QVBoxLayout *into)
{
    QWidget *parent = into->parentWidget();
    const QString cls = m_doc->link().classKey;
    auto *l = new QLabel(cls.isEmpty() ? tr("<b>%1</b> is not linked to a class.").arg(m_doc->fileName().toHtmlEscaped())
                                       : tr("The class <b>%1</b> of %2 was not found.")
                                             .arg(cls.toHtmlEscaped(), m_doc->fileName().toHtmlEscaped()),
                         parent);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    auto *hint = new QLabel(tr("Link it to the class that describes its items, or edit it as plain JSON "
                               "(any value, no checks)."),
                            parent);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("dimLabel"));
    auto *buttons = new QHBoxLayout();
    auto *link = new QPushButton(tr("Link to Class…"), parent);
    auto *plain = new QPushButton(tr("Edit as Plain JSON"), parent);
    connect(link, &QPushButton::clicked, this, &Inspector::changeClassRequested);
    connect(plain, &QPushButton::clicked, this, &Inspector::plainRequested);
    buttons->addStretch(1);
    buttons->addWidget(link);
    buttons->addWidget(plain);
    buttons->addStretch(1);
    into->addStretch(1);
    into->addWidget(l);
    into->addWidget(hint);
    into->addLayout(buttons);
    into->addStretch(2);
}

namespace {

enum PlainType { PNull, PBool, PInt, PFloat, PString, PArray, PObject };

int plainTypeOf(const JValue &v)
{
    switch (v.type()) {
    case JValue::Bool: return PBool;
    case JValue::Number: return v.hasIntLexeme() ? PInt : PFloat;
    case JValue::String: return PString;
    case JValue::Array: return PArray;
    case JValue::Object: return PObject;
    default: return PNull;
    }
}

// Converts a value to another JSON type, keeping what can be kept.
JValue convertPlain(const JValue &v, int type)
{
    switch (type) {
    case PBool:
        return JValue(v.isNumber() ? v.toDouble() != 0.0 : v.isString() ? v.toString() == QLatin1String("true") : v.toBool());
    case PInt:
        if (v.isNumber())
            return JValue(qint64(std::llround(v.toDouble())));
        return JValue(v.isString() ? v.toString().trimmed().toLongLong() : qint64(v.toBool() ? 1 : 0));
    case PFloat:
        if (v.isNumber())
            return JValue::real(v.toDouble());
        return JValue::real(v.isString() ? v.toString().trimmed().toDouble() : (v.toBool() ? 1.0 : 0.0));
    case PString:
        if (v.isString())
            return v;
        return JValue((v.isBool() || v.isNumber()) ? JsonIO::writeCompact(v) : QString());
    case PArray: {
        if (v.isArray())
            return v;
        JValue a = JValue::array();
        if (v.isObject()) {
            for (const JMember &m : v.members())
                a.append(m.value);
        } else if (!v.isNull() && !v.isUndefined()) {
            a.append(v);
        }
        return a;
    }
    case PObject:
        return v.isObject() ? v : JValue::object();
    default:
        return JValue::null();
    }
}

// An empty value of the same type, used for new elements.
JValue blankLike(const JValue &v)
{
    switch (plainTypeOf(v)) {
    case PNull: return JValue::null();
    case PBool: return JValue(false);
    case PInt: return JValue(0);
    case PFloat: return JValue::real(0.0);
    case PArray: return JValue::array();
    case PObject: return JValue::object();
    default: return JValue(QString());
    }
}

QString containerSummary(const JValue &v)
{
    return v.isObject() ? Inspector::tr("%n field(s)", nullptr, v.size()) : Inspector::tr("%n element(s)", nullptr, v.size());
}

} // namespace

void Inspector::buildPlainHeader(QVBoxLayout *into)
{
    QWidget *parent = into->parentWidget();
    const JValue v = m_doc->item(m_item).value;
    auto *box = new QWidget(parent);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(16, 14, 16, 14);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(Theme::RowSpacing);
    box->setObjectName(QStringLiteral("itemHeader"));
    box->setAttribute(Qt::WA_StyledBackground, true);
    int r = 0;

    auto *title = new QLabel(m_doc->itemTitle(m_item), box);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.6);
    tf.setBold(true);
    title->setFont(tf);
    grid->addWidget(title, r++, 0, 1, 3);

    if (m_doc->shape() == JsonDocument::Shape::Dictionary) {
        grid->addWidget(makeLabel(tr("Key"), 0, box), r, 0);
        grid->addWidget(itemKeyEditor(box), r++, 1, 1, 2);
    }

    grid->addWidget(makeLabel(tr("Class"), 0, box), r, 0);
    auto *cl = new QLabel(tr("None: plain JSON"), box);
    cl->setToolTip(tr("This file is not linked to a class: every value can be edited freely and nothing is checked."));
    grid->addWidget(cl, r, 1);
    auto *change = new QPushButton(tr("Link to Class…"), box);
    change->setToolTip(tr("Link %1 to a class so its values are checked").arg(m_doc->fileName()));
    connect(change, &QPushButton::clicked, this, &Inspector::changeClassRequested);
    grid->addWidget(change, r++, 2);

    if (v.isContainer()) {
        // The type of the whole item, and a button to add a field / element to it.
        grid->addWidget(makeLabel(tr("Value"), 0, box), r, 0);
        auto *row = new QHBoxLayout();
        row->setSpacing(Theme::FieldSpacing);
        row->addWidget(plainTypeCombo({}, v, box));
        auto *count = new QLabel(v.isObject() ? tr("%n field(s)", nullptr, v.size()) : tr("%n element(s)", nullptr, v.size()), box);
        count->setObjectName(QStringLiteral("dimLabel"));
        row->addWidget(count, 1);
        auto *add = plainAddButton({}, box, nullptr);
        row->addWidget(add);
        grid->addLayout(row, r++, 1, 1, 2);
    }
    grid->setColumnStretch(1, 1);
    into->addWidget(box);
}

QComboBox *Inspector::plainTypeCombo(const JPath &path, const JValue &v, QWidget *parent)
{
    auto *type = new QComboBox(parent);
    WheelGuard::install(type);
    type->addItems({tr("null"), tr("bool"), tr("int"), tr("float"), tr("String"), tr("Array"), tr("Object")});
    type->setCurrentIndex(plainTypeOf(v));
    type->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    type->setToolTip(tr("JSON type of this value"));
    connect(type, qOverload<int>(&QComboBox::activated), this, [this, path](int i) {
        const JValue cur = stored(path);
        if (plainTypeOf(cur) == i)
            return;
        commitStructural(path, convertPlain(cur, i), tr("Change type"));
    });
    return type;
}

QToolButton *Inspector::plainAddButton(const JPath &path, QWidget *parent, CollapsibleSection *sec)
{
    const bool isObject = stored(path).isObject();
    auto *add = makeToolButton(QStringLiteral("+"), isObject ? tr("Add a field") : tr("Add an element"), parent);
    connect(add, &QToolButton::clicked, this, [this, path, sec] {
        JValue c = stored(path);
        if (c.isObject()) {
            QString k = QStringLiteral("key");
            for (int n = 2; c.contains(k); ++n)
                k = QStringLiteral("key_%1").arg(n);
            c.set(k, JValue(QString()));
        } else if (c.isArray()) {
            c.append(c.size() > 0 ? blankLike(c.at(c.size() - 1)) : JValue(QString()));
        } else {
            return;
        }
        if (sec) {
            sec->setExpanded(true);
            m_expanded.insert(sec->property("stateKey").toString(), true);
        }
        commitStructural(path, c, c.isObject() ? tr("Add field") : tr("Add element"));
    });
    return add;
}

void Inspector::buildPlainBody(QVBoxLayout *into, const JPath &path, const JValue &v, int depth)
{
    if (v.isObject()) {
        for (const JMember &m : v.members()) {
            const QString key = m.key;
            auto *keyEdit = new QLineEdit(key, m_content);
            keyEdit->setFixedWidth(labelWidth(depth));
            keyEdit->setToolTip(tr("Field name"));
            connect(keyEdit, &QLineEdit::editingFinished, this, [this, keyEdit, key, path] {
                const QString nk = keyEdit->text();
                if (nk == key)
                    return;
                JValue d = stored(path);
                QString err;
                if (nk.isEmpty())
                    err = tr("Field names cannot be empty.");
                else if (d.contains(nk))
                    err = tr("The field \"%1\" already exists.").arg(nk);
                if (!err.isEmpty()) {
                    keyEdit->setText(key);
                    emit statusMessage(err);
                    return;
                }
                d.renameKey(key, nk);
                commitStructural(path, d, tr("Rename field"));
            });
            auto extras = [this, path, key](QWidget *parent, QHBoxLayout *hl) {
                auto *rm = makeToolButton(QStringLiteral("✕"), tr("Remove \"%1\"").arg(key), parent);
                connect(rm, &QToolButton::clicked, this, [this, path, key] {
                    JValue d = stored(path);
                    d.remove(key);
                    commitStructural(path, d, tr("Remove %1").arg(key));
                });
                hl->addWidget(rm);
            };
            into->addWidget(plainValue(path + PathSeg::k(key), keyEdit, m.value, depth, extras));
        }
    } else if (v.isArray()) {
        const int n = v.size();
        for (int i = 0; i < n; ++i) {
            auto *idx = new QLabel(QString::number(i), m_content);
            idx->setObjectName(QStringLiteral("dimLabel"));
            idx->setFixedWidth(34);
            auto extras = [this, path, i, n](QWidget *parent, QHBoxLayout *hl) {
                auto edit = [this, path](std::function<void(JValue &)> fn, const QString &text) {
                    JValue a = stored(path);
                    fn(a);
                    commitStructural(path, a, text);
                };
                auto *up = makeToolButton(QStringLiteral("▲"), tr("Move up"), parent);
                up->setEnabled(i > 0);
                auto *down = makeToolButton(QStringLiteral("▼"), tr("Move down"), parent);
                down->setEnabled(i < n - 1);
                auto *dup = makeToolButton(QStringLiteral("⧉"), tr("Duplicate"), parent);
                auto *rm = makeToolButton(QStringLiteral("✕"), tr("Remove"), parent);
                connect(up, &QToolButton::clicked, this, [edit, i] {
                    edit([i](JValue &a) { std::swap(a.elements()[size_t(i)], a.elements()[size_t(i - 1)]); }, tr("Move element"));
                });
                connect(down, &QToolButton::clicked, this, [edit, i] {
                    edit([i](JValue &a) { std::swap(a.elements()[size_t(i)], a.elements()[size_t(i + 1)]); }, tr("Move element"));
                });
                connect(dup, &QToolButton::clicked, this, [edit, i] {
                    edit([i](JValue &a) { a.insert(i + 1, a.at(i)); }, tr("Duplicate element"));
                });
                connect(rm, &QToolButton::clicked, this, [edit, i] {
                    edit([i](JValue &a) { a.removeAt(i); }, tr("Remove element"));
                });
                hl->addWidget(up);
                hl->addWidget(down);
                hl->addWidget(dup);
                hl->addWidget(rm);
            };
            into->addWidget(plainValue(path + PathSeg::i(i), idx, v.at(i), depth, extras));
        }
    }
    if (v.isContainer() && v.size() == 0) {
        auto *empty = new QLabel(tr("Empty"), m_content);
        empty->setObjectName(QStringLiteral("dimLabel"));
        into->addWidget(empty);
    }
}

QWidget *Inspector::plainValue(const JPath &path, QWidget *label, const JValue &v, int depth, const ExtraButtons &extras)
{
    Row row;
    row.path = path;
    row.revertable = false;
    row.label = qobject_cast<QLabel *>(label);

    if (v.isContainer()) {
        auto *sec = new CollapsibleSection(QString(), CollapsibleSection::Value, m_content);
        trackSection(sec, QStringLiteral("plain:") + pathPattern(path), depth < 1);
        // [▾] [key] [type] n fields ........ [+] [buttons]
        auto *title = new QWidget(sec);
        auto *tl = new QHBoxLayout(title);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(Theme::FieldSpacing);
        if (label->minimumWidth() > 60)
            label->setFixedWidth(label->minimumWidth() - 28); // room for the arrow, so values stay aligned
        label->setParent(title);
        tl->addWidget(label);
        tl->addWidget(plainTypeCombo(path, v, title));
        sec->setTitleWidget(title);
        sec->setSubtitle(containerSummary(v));
        sec->headerLayout()->addWidget(plainAddButton(path, sec, sec));
        if (extras)
            extras(sec, sec->headerLayout());
        buildPlainBody(sec->bodyLayout(), path, v, depth + 1);
        row.anchor = sec;
        row.section = sec;
        m_rows << row;
        return sec;
    }

    auto *line = new QWidget(m_content);
    auto *hl = new QHBoxLayout(line);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::FieldSpacing);
    label->setParent(line);
    hl->addWidget(label);
    hl->addWidget(plainTypeCombo(path, v, line));
    QWidget *ed = nullptr;
    switch (plainTypeOf(v)) {
    case PBool: {
        auto *cb = new QCheckBox(tr("On"), line);
        cb->setChecked(v.toBool());
        connect(cb, &QCheckBox::toggled, this, [this, path](bool b) { commit(path, JValue(b), tr("Toggle"), false); });
        ed = cb;
        break;
    }
    case PInt:
        ed = intEditor(path, TypeRef(), PropertyHint(), v);
        break;
    case PFloat:
        ed = floatEditor(path, PropertyHint(), v);
        break;
    case PString: {
        auto *le = new QLineEdit(v.toString(), line);
        connect(le, &QLineEdit::editingFinished, this, [this, le, path] {
            commit(path, JValue(le->text()), tr("Edit text"), false);
        });
        ed = le;
        break;
    }
    default: {
        auto *nl = new QLabel(QStringLiteral("null"), line);
        nl->setObjectName(QStringLiteral("dimLabel"));
        ed = nl;
    }
    }
    ed->setParent(line);
    hl->addWidget(ed, 1);
    if (extras)
        extras(line, hl);
    row.anchor = line;
    m_rows << row;
    return line;
}
