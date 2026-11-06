#include "Dialogs.h"
#include "AppSettings.h"
#include "model/Project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------- JsonPicker

JsonPickerDialog::JsonPickerDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Select a JSON file"));
    resize(640, 480);
    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Choose the JSON file to edit. Files that are not linked yet will ask for a class next "
                                  "(or to be edited as plain JSON)."), this);
    intro->setWordWrap(true);
    l->addWidget(intro);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Filter…"));
    m_search->setClearButtonEnabled(true);
    l->addWidget(m_search);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("File"), tr("Class")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    l->addWidget(m_tree, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto accept = [this] {
        if (auto *it = m_tree->currentItem()) {
            m_selected = it->data(0, Qt::UserRole).toString();
            QDialog::accept();
        }
    };
    connect(bb, &QDialogButtonBox::accepted, this, accept);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, accept);
    connect(m_search, &QLineEdit::textChanged, this, &JsonPickerDialog::fill);
    fill();
}

void JsonPickerDialog::fill()
{
    m_tree->clear();
    const QString needle = m_search->text().trimmed();
    for (const QString &res : m_project->jsonFiles()) {
        if (!needle.isEmpty() && !res.contains(needle, Qt::CaseInsensitive))
            continue;
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, res.mid(6));
        it->setData(0, Qt::UserRole, res);
        auto link = m_project->config().links.constFind(res);
        if (link != m_project->config().links.cend()) {
            it->setText(1, link->plain ? tr("Plain JSON") : m_project->schema().displayName(link->classKey));
        } else {
            it->setText(1, tr("— not linked —"));
            it->setForeground(1, palette().brush(QPalette::PlaceholderText));
        }
    }
    if (m_tree->topLevelItemCount() > 0)
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
}

// ---------------------------------------------------------------- ClassPicker

ClassPickerDialog::ClassPickerDialog(Project *project, const QString &jsonResPath, const LinkInfo &current,
                                     QWidget *parent)
    : QDialog(parent), m_project(project), m_json(jsonResPath)
{
    setWindowTitle(tr("Select the class of %1").arg(jsonResPath.section(QLatin1Char('/'), -1)));
    resize(900, 600);
    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Which class describes the objects stored in <b>%1</b>? "
                                "Only the variables of that class (and its parents) will be allowed in the file.<br>"
                                "If the file is not made of class objects, choose <b>Edit as Plain JSON</b> to edit "
                                "any value freely, without checks.")
                                 .arg(jsonResPath.toHtmlEscaped()),
                             this);
    intro->setWordWrap(true);
    l->addWidget(intro);

    auto *split = new QSplitter(this);
    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(tr("Search classes…"));
    m_search->setClearButtonEnabled(true);
    ll->addWidget(m_search);
    m_tree = new QTreeWidget(left);
    m_tree->setHeaderLabels({tr("Class"), tr("Extends"), tr("File")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ll->addWidget(m_tree, 1);
    auto *browse = new QPushButton(tr("Browse for a script file…"), left);
    ll->addWidget(browse);

    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    m_info = new QLabel(right);
    m_info->setWordWrap(true);
    rl->addWidget(m_info);
    m_fields = new QTreeWidget(right);
    m_fields->setHeaderLabels({tr("Field"), tr("Type"), tr("Restriction"), tr("Declared in")});
    m_fields->setRootIsDecorated(false);
    rl->addWidget(m_fields, 1);
    auto *opts = new QGroupBox(tr("How items are stored in the file"), right);
    auto *form = new QFormLayout(opts);
    m_shape = new QComboBox(opts);
    m_shape->addItem(tr("Dictionary: { \"id\": {item}, … }"), QStringLiteral("dictionary"));
    m_shape->addItem(tr("Array: [ {item}, … ]"), QStringLiteral("array"));
    m_shape->addItem(tr("Single object: the whole file is one item"), QStringLiteral("single"));
    form->addRow(tr("Layout"), m_shape);
    m_keyField = new QComboBox(opts);
    m_keyField->setToolTip(tr("Dictionary layout: the class variable that receives the key (e.g. id).\n"
                              "It is hidden from the inspector and must not be repeated inside the item."));
    form->addRow(tr("Key fills field"), m_keyField);
    rl->addWidget(opts);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    l->addWidget(split, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Link"));
    auto *plain = bb->addButton(tr("Edit as Plain JSON"), QDialogButtonBox::ActionRole);
    plain->setToolTip(tr("Do not link the file to a class: edit it as plain JSON, with no validation."));
    connect(plain, &QPushButton::clicked, this, [this] {
        m_plain = true;
        accept();
    });
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        if (currentKey().isEmpty()) {
            QMessageBox::information(this, windowTitle(), tr("Select a class first."));
            return;
        }
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, &ClassPickerDialog::fill);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ClassPickerDialog::updateDetails);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [bb](QTreeWidgetItem *it) {
        if (it && !it->data(0, Qt::UserRole).toString().isEmpty())
            bb->button(QDialogButtonBox::Ok)->click();
    });
    connect(m_shape, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_keyField->setEnabled(m_shape->currentData().toString() == QLatin1String("dictionary"));
    });
    connect(browse, &QPushButton::clicked, this, &ClassPickerDialog::browseScript);

    // Detect the current shape of the file.
    QString err;
    JsonDocument probe(project->toAbs(jsonResPath), jsonResPath);
    if (probe.load(&err))
        m_detectedShape = JsonDocument::shapeName(JsonDocument::detectShape(probe.root(), {}));
    m_initialKeyField = current.keyField;
    fill();
    if (!current.classKey.isEmpty())
        selectKey(current.classKey);
    const QString shape = current.shape.isEmpty() ? m_detectedShape : current.shape;
    m_shape->setCurrentIndex(qMax(0, m_shape->findData(shape)));
}

QString ClassPickerDialog::currentKey() const
{
    auto *it = m_tree->currentItem();
    return it ? it->data(0, Qt::UserRole).toString() : QString();
}

void ClassPickerDialog::selectKey(const QString &key)
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == key) {
            m_tree->setCurrentItem(*it);
            m_tree->scrollToItem(*it);
            return;
        }
}

void ClassPickerDialog::fill()
{
    const QString keep = currentKey();
    m_tree->clear();
    const QString needle = m_search->text().trimmed();
    ClassRegistry &reg = m_project->registry();
    auto *gd = new QTreeWidgetItem(m_tree, {tr("Project classes (GDScript)")});
    auto *cs = new QTreeWidgetItem(m_tree, {tr("Project classes (C#)")});
    auto *bi = new QTreeWidgetItem(m_tree, {tr("Godot built-in classes")});
    for (QTreeWidgetItem *g : {gd, cs, bi}) {
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFlags(Qt::ItemIsEnabled);
    }
    auto add = [&](const ClassInfo *c, QTreeWidgetItem *group) {
        const QString name = m_project->schema().displayName(c->key);
        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive) && !c->scriptPath.contains(needle, Qt::CaseInsensitive))
            return;
        auto *it = new QTreeWidgetItem(group);
        it->setText(0, name);
        const ClassInfo *b = reg.base(c);
        it->setText(1, b ? m_project->schema().displayName(b->key) : c->baseRaw);
        it->setText(2, c->scriptPath);
        it->setData(0, Qt::UserRole, c->key);
        if (!c->global && !c->builtin)
            it->setToolTip(0, tr("Script without class_name: referenced by path"));
    };
    for (const ClassInfo *c : reg.scriptClasses())
        add(c, c->language == QLatin1String("csharp") ? cs : gd);
    for (const ClassInfo *c : reg.builtinClasses())
        add(c, bi);
    gd->setExpanded(true);
    cs->setExpanded(true);
    bi->setExpanded(!needle.isEmpty());
    if (cs->childCount() == 0)
        delete cs;
    if (!keep.isEmpty())
        selectKey(keep);
    if (currentKey().isEmpty() && !needle.isEmpty()) {
        // Select the first match so its fields are shown right away.
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if (!(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
                m_tree->setCurrentItem(*it);
                break;
            }
    }
}

void ClassPickerDialog::updateDetails()
{
    m_fields->clear();
    const QString key = currentKey();
    ClassRegistry &reg = m_project->registry();
    const ClassInfo *c = reg.get(key);
    if (!c) {
        m_info->setText(tr("Select a class to see its fields."));
        return;
    }
    QStringList chain;
    for (const ClassInfo *k : reg.chain(key))
        chain << m_project->schema().displayName(k->key);
    QString info = QStringLiteral("<b>%1</b><br>%2").arg(chain.first().toHtmlEscaped(), chain.join(QStringLiteral(" ‹ ")).toHtmlEscaped());
    if (!c->doc.isEmpty())
        info += QStringLiteral("<br><i>%1</i>").arg(c->doc.toHtmlEscaped());
    const QStringList subs = reg.descendants(key);
    if (!subs.isEmpty()) {
        QStringList names;
        for (const QString &s : subs)
            names << m_project->schema().displayName(s);
        info += tr("<br>Subclasses: %1").arg(names.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    m_info->setText(info);
    const QString keepKey = m_keyField->currentData().toString().isEmpty() ? m_initialKeyField : m_keyField->currentData().toString();
    m_keyField->clear();
    m_keyField->addItem(tr("(none)"), QString());
    for (const ResolvedProperty &p : reg.properties(key)) {
        auto *it = new QTreeWidgetItem(m_fields);
        it->setText(0, p.info.name);
        it->setText(1, p.type.displayName());
        QString r;
        switch (p.info.hint.kind) {
        case PropertyHint::Range:
            r = QStringLiteral("%1 – %2").arg(p.info.hint.hasMin ? QString::number(p.info.hint.min) : QStringLiteral("…"),
                                             p.info.hint.hasMax ? QString::number(p.info.hint.max) : QStringLiteral("…"));
            if (p.info.hint.orGreater) r += QStringLiteral("+");
            break;
        case PropertyHint::Enum: r = tr("%1 options").arg(p.info.hint.options.size()); break;
        case PropertyHint::Flags: r = tr("flags"); break;
        case PropertyHint::File: r = p.info.hint.filters.join(QLatin1Char(' ')); break;
        default: break;
        }
        if (!p.type.enumKey.isEmpty())
            r = tr("enum");
        it->setText(2, r);
        it->setText(3, m_project->schema().displayName(p.ownerKey));
        if (reg.get(p.ownerKey) && reg.get(p.ownerKey)->builtin)
            for (int col = 0; col < 4; ++col)
                it->setForeground(col, palette().brush(QPalette::PlaceholderText));
        if (p.type.isStringLike())
            m_keyField->addItem(p.info.name, p.info.name);
    }
    for (int i = 0; i < 4; ++i)
        m_fields->resizeColumnToContents(i);
    int idx = m_keyField->findData(keepKey);
    if (idx < 0 && keepKey.isEmpty())
        idx = m_keyField->findData(QStringLiteral("id"));
    if (idx < 0 && !keepKey.isEmpty()) {
        // The key field is usually filled by code, so it need not be an editable (exported) variable.
        m_keyField->addItem(keepKey, keepKey);
        idx = m_keyField->count() - 1;
    }
    m_keyField->setCurrentIndex(qMax(0, idx));
}

void ClassPickerDialog::browseScript()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a script"), m_project->root(),
                                                      tr("Godot scripts (*.gd *.cs);;All files (*)"));
    if (file.isEmpty())
        return;
    const QString key = m_project->registry().addScript(file);
    if (key.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("No class could be read from %1.").arg(file));
        return;
    }
    m_search->clear();
    fill();
    selectKey(key);
}

LinkInfo ClassPickerDialog::link() const
{
    LinkInfo l;
    l.json = m_json;
    if (m_plain) {
        l.plain = true;
        l.shape = m_shape->currentData().toString();
        return l;
    }
    l.classKey = currentKey();
    if (const ClassInfo *c = m_project->registry().get(l.classKey))
        l.scriptPath = c->scriptPath;
    l.shape = m_shape->currentData().toString();
    if (l.shape == QLatin1String("dictionary"))
        l.keyField = m_keyField->currentData().toString();
    return l;
}

// ---------------------------------------------------------------- Settings

SettingsDialog::SettingsDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Settings"));
    resize(560, 380);
    auto *l = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    l->addWidget(tabs);

    auto *proj = new QWidget(tabs);
    auto *pf = new QFormLayout(proj);
    m_vector = new QComboBox(proj);
    m_vector->addItem(QStringLiteral("[x, y]"), QStringLiteral("array"));
    m_vector->addItem(QStringLiteral("{\"x\": x, \"y\": y}"), QStringLiteral("object"));
    m_vector->addItem(QStringLiteral("\"(x, y)\""), QStringLiteral("string"));
    m_color = new QComboBox(proj);
    m_color->addItem(QStringLiteral("\"#rrggbbaa\""), QStringLiteral("hex"));
    m_color->addItem(QStringLiteral("[r, g, b, a]"), QStringLiteral("array"));
    m_color->addItem(QStringLiteral("{\"r\", \"g\", \"b\", \"a\"}"), QStringLiteral("object"));
    m_scope = new QComboBox(proj);
    m_scope->addItem(tr("Exported only (@export / [Export])"), varScopeName(VarScope::Exported));
    m_scope->addItem(tr("Exported and public (not starting with _)"), varScopeName(VarScope::Public));
    m_scope->addItem(tr("All variables, private ones included"), varScopeName(VarScope::All));
    m_scope->setToolTip(tr("Which variables of a class can be edited and stored in the JSON.\n"
                           "Godot itself only saves exported variables."));
    m_backups = new QCheckBox(tr("Write a .bak copy before saving a JSON file"), proj);
    pf->addRow(tr("New vectors are written as"), m_vector);
    pf->addRow(tr("New colors are written as"), m_color);
    pf->addRow(tr("Editable variables"), m_scope);
    pf->addRow(QString(), m_backups);
    auto *note = new QLabel(tr("Existing values keep the format they already have. "
                               "These settings are stored in ginspector.json."), proj);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("dimLabel"));
    pf->addRow(note);
    tabs->addTab(proj, tr("Project"));
    proj->setEnabled(project->isOpen());
    if (project->isOpen()) {
        const ProjectConfig &c = project->config();
        m_vector->setCurrentIndex(m_vector->findData(ValueFormat::vectorName(c.format.vector)));
        m_color->setCurrentIndex(m_color->findData(ValueFormat::colorName(c.format.color)));
        m_scope->setCurrentIndex(qMax(0, m_scope->findData(varScopeName(c.varScope))));
        m_backups->setChecked(c.backups);
    }

    auto *godot = new QWidget(tabs);
    auto *gl = new QVBoxLayout(godot);
    auto *intro = new QLabel(tr("Built-in classes (Resource, Node2D, Texture2D…) come from a bundled list of common classes. "
                                "Point to your Godot executable to read the exact list of every built-in class and property "
                                "of your Godot version."), godot);
    intro->setWordWrap(true);
    gl->addWidget(intro);
    auto *row = new QHBoxLayout();
    m_godot = new QLineEdit(AppSettings::godotExecutable(), godot);
    m_godot->setPlaceholderText(tr("Path to the Godot executable"));
    auto *browse = new QPushButton(tr("Browse…"), godot);
    row->addWidget(m_godot, 1);
    row->addWidget(browse);
    gl->addLayout(row);
    auto *row2 = new QHBoxLayout();
    auto *gen = new QPushButton(tr("Read classes from Godot"), godot);
    auto *bundled = new QPushButton(tr("Use bundled list"), godot);
    row2->addWidget(gen);
    row2->addWidget(bundled);
    row2->addStretch(1);
    gl->addLayout(row2);
    m_apiLabel = new QLabel(godot);
    m_apiLabel->setWordWrap(true);
    gl->addWidget(m_apiLabel);
    gl->addStretch(1);
    tabs->addTab(godot, tr("Godot"));
    updateApiLabel();

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Godot executable"), m_godot->text());
        if (!f.isEmpty())
            m_godot->setText(f);
    });
    connect(gen, &QPushButton::clicked, this, &SettingsDialog::generateApi);
    connect(bundled, &QPushButton::clicked, this, [this] {
        AppSettings::setExtensionApiPath(QString());
        m_project->loadBuiltins(QString());
        updateApiLabel();
    });

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::updateApiLabel()
{
    m_apiLabel->setText(tr("Current source: %1").arg(m_project->registry().builtinSource()));
}

void SettingsDialog::generateApi()
{
    const QString exe = m_godot->text().trimmed();
    if (exe.isEmpty() || !QFileInfo(exe).isExecutable()) {
        QMessageBox::warning(this, windowTitle(), tr("Choose the Godot executable first."));
        return;
    }
    AppSettings::setGodotExecutable(exe);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/godot_api");
    QDir().mkpath(dir);
    QFile::remove(dir + QStringLiteral("/extension_api.json"));
    QProcess proc;
    proc.setWorkingDirectory(dir);
    setCursor(Qt::WaitCursor);
    proc.start(exe, {QStringLiteral("--headless"), QStringLiteral("--dump-extension-api")});
    const bool finished = proc.waitForFinished(120000);
    unsetCursor();
    const QString out = dir + QStringLiteral("/extension_api.json");
    if (!finished || !ClassRegistry::isValidExtensionApi(out)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Godot did not produce extension_api.json.\n\n%1")
                                 .arg(QString::fromLocal8Bit(proc.readAllStandardError()).left(800)));
        return;
    }
    AppSettings::setExtensionApiPath(out);
    m_project->loadBuiltins(out);
    updateApiLabel();
}

void SettingsDialog::apply()
{
    AppSettings::setGodotExecutable(m_godot->text().trimmed());
    if (!m_project->isOpen())
        return;
    ProjectConfig &c = m_project->config();
    const auto vec = ValueFormat::vectorFromName(m_vector->currentData().toString());
    const auto col = ValueFormat::colorFromName(m_color->currentData().toString());
    const VarScope scope = varScopeFromName(m_scope->currentData().toString());
    m_projectChanged = vec != c.format.vector || col != c.format.color || scope != c.varScope
                       || m_backups->isChecked() != c.backups;
    c.format.vector = vec;
    c.format.color = col;
    c.varScope = scope;
    c.backups = m_backups->isChecked();
    if (m_projectChanged) {
        QString err;
        if (!m_project->saveConfig(&err))
            QMessageBox::warning(this, windowTitle(), err);
        m_project->reloadScripts();
    }
}

// ---------------------------------------------------------------- Polymorphism

PolymorphismDialog::PolymorphismDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Polymorphic types"));
    resize(720, 460);
    m_edit = project->config().poly;
    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("When a field is declared as a base class (e.g. <i>Array[ItemEffect]</i>), each JSON object "
                                "says which subclass it is through a key such as <code>\"type\": \"alter_stat\"</code>. "
                                "By default the key is <code>type</code> and the values are the class names in snake_case "
                                "without the suffix shared with the base class."), this);
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *split = new QSplitter(this);
    m_bases = new QListWidget(split);
    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    m_explicit = new QCheckBox(tr("Custom configuration (otherwise automatic)"), right);
    rl->addWidget(m_explicit);
    auto *kr = new QHBoxLayout();
    kr->addWidget(new QLabel(tr("Discriminator key"), right));
    m_key = new QLineEdit(right);
    kr->addWidget(m_key, 1);
    rl->addLayout(kr);
    m_table = new QTableWidget(0, 2, right);
    m_table->setHorizontalHeaderLabels({tr("JSON value"), tr("Class")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    rl->addWidget(m_table, 1);
    auto *autoBtn = new QPushButton(tr("Fill automatic names"), right);
    rl->addWidget(autoBtn);
    split->addWidget(m_bases);
    split->addWidget(right);
    split->setStretchFactor(1, 2);
    l->addWidget(split, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    l->addWidget(bb);

    ClassRegistry &reg = project->registry();
    for (const ClassInfo *c : reg.scriptClasses()) {
        if (reg.descendants(c->key).isEmpty())
            continue;
        // Only roots of hierarchies (their parent has no other script subclasses configured)
        auto *it = new QListWidgetItem(project->schema().displayName(c->key), m_bases);
        it->setData(Qt::UserRole, c->key);
    }
    connect(m_bases, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur, QListWidgetItem *) {
        storeCurrent();
        loadBase(cur ? cur->data(Qt::UserRole).toString() : QString());
    });
    connect(m_explicit, &QCheckBox::toggled, this, [this](bool on) {
        m_key->setEnabled(on);
        m_table->setEnabled(on);
    });
    connect(autoBtn, &QPushButton::clicked, this, &PolymorphismDialog::autoFill);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        storeCurrent();
        save();
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (m_bases->count() > 0)
        m_bases->setCurrentRow(0);
    else {
        m_explicit->setEnabled(false);
        m_key->setEnabled(false);
        m_table->setEnabled(false);
        autoBtn->setEnabled(false);
    }
}

void PolymorphismDialog::loadBase(const QString &key)
{
    m_current = key;
    m_table->setRowCount(0);
    if (key.isEmpty())
        return;
    const bool exp = m_edit.contains(key);
    const Schema::Poly p = exp ? Schema::Poly{key, m_edit[key].key, m_edit[key].types, false}
                               : m_project->schema().autoPoly(key);
    m_explicit->setChecked(exp);
    m_key->setEnabled(exp);
    m_table->setEnabled(exp);
    m_key->setText(p.key);
    QStringList classes;
    classes << key << m_project->registry().descendants(key);
    m_table->setRowCount(classes.size());
    for (int r = 0; r < classes.size(); ++r) {
        QString value;
        for (const auto &t : p.types)
            if (t.second == classes[r])
                value = t.first;
        m_table->setItem(r, 0, new QTableWidgetItem(value));
        auto *ci = new QTableWidgetItem(m_project->schema().displayName(classes[r]));
        ci->setData(Qt::UserRole, classes[r]);
        ci->setFlags(ci->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(r, 1, ci);
    }
}

void PolymorphismDialog::storeCurrent()
{
    if (m_current.isEmpty())
        return;
    if (!m_explicit->isChecked()) {
        m_edit.remove(m_current);
        return;
    }
    PolyConfig c;
    c.key = m_key->text().trimmed().isEmpty() ? QStringLiteral("type") : m_key->text().trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QString v = m_table->item(r, 0)->text().trimmed();
        if (!v.isEmpty())
            c.types << qMakePair(v, m_table->item(r, 1)->data(Qt::UserRole).toString());
    }
    m_edit.insert(m_current, c);
}

void PolymorphismDialog::autoFill()
{
    if (m_current.isEmpty())
        return;
    const Schema::Poly p = m_project->schema().autoPoly(m_current);
    m_explicit->setChecked(true);
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QString cls = m_table->item(r, 1)->data(Qt::UserRole).toString();
        for (const auto &t : p.types)
            if (t.second == cls)
                m_table->item(r, 0)->setText(t.first);
    }
    if (m_key->text().isEmpty())
        m_key->setText(p.key);
}

void PolymorphismDialog::save()
{
    m_project->config().poly = m_edit;
    QString err;
    if (!m_project->saveConfig(&err))
        QMessageBox::warning(this, windowTitle(), err);
    m_project->reloadScripts();
}
