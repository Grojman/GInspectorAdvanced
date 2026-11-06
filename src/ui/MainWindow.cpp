#include "MainWindow.h"
#include "AppSettings.h"
#include "CardList.h"
#include "Dialogs.h"
#include "Inspector.h"
#include "Theme.h"
#include "model/Commands.h"
#include "model/Project.h"
#include "model/Validator.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoGroup>
#include <QUndoStack>
#include <QVBoxLayout>

namespace {

QIcon stdIcon(QStyle::StandardPixmap sp, const QString &theme = QString())
{
    if (!theme.isEmpty() && QIcon::hasThemeIcon(theme))
        return QIcon::fromTheme(theme);
    return QApplication::style()->standardIcon(sp);
}

QAction *makeAction(QObject *parent, const QString &text, const QKeySequence &key, const QIcon &icon = QIcon())
{
    auto *a = new QAction(icon, text, parent);
    if (!key.isEmpty())
        a->setShortcut(key);
    return a;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_project(new Project(this)), m_undoGroup(new QUndoGroup(this))
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    const QString api = AppSettings::extensionApiPath();
    if (!api.isEmpty())
        m_project->registry().loadBuiltins(api);
    m_problemTimer = new QTimer(this);
    m_problemTimer->setSingleShot(true);
    m_problemTimer->setInterval(150);
    connect(m_problemTimer, &QTimer::timeout, this, &MainWindow::refreshProblems);

    buildActions();
    buildUi();
    buildMenus();

    connect(m_project, &Project::schemaChanged, this, [this] {
        m_cards->refreshIssues();
        m_inspector->rebuild();
        rebuildAddMenu();
        refreshProblems();
        m_builtinLabel->setText(m_project->registry().builtinSource());
    });
    connect(m_project, &Project::linksChanged, this, &MainWindow::refreshFileTree);

    restoreGeometry(AppSettings::windowState(QStringLiteral("geometry")));
    restoreState(AppSettings::windowState(QStringLiteral("state")));
    if (!restoreGeometry(AppSettings::windowState(QStringLiteral("geometry"))))
        resize(1280, 800);
    updateTitle();
    updateActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildActions()
{
    m_actOpenProject = makeAction(this, tr("&Open Godot Project…"), QKeySequence(tr("Ctrl+Shift+O")),
                                  stdIcon(QStyle::SP_DirOpenIcon, QStringLiteral("folder-open")));
    m_actCloseProject = makeAction(this, tr("&Close Project"), QKeySequence());
    m_actOpenJson = makeAction(this, tr("Open &JSON File…"), QKeySequence::Open,
                               stdIcon(QStyle::SP_FileIcon, QStringLiteral("document-open")));
    m_actSave = makeAction(this, tr("&Save"), QKeySequence::Save, stdIcon(QStyle::SP_DialogSaveButton, QStringLiteral("document-save")));
    m_actSaveAll = makeAction(this, tr("Save A&ll"), QKeySequence(tr("Ctrl+Shift+S")),
                              stdIcon(QStyle::SP_DialogSaveButton, QStringLiteral("document-save-all")));
    m_actQuit = makeAction(this, tr("&Quit"), QKeySequence::Quit);
    m_actUndo = m_undoGroup->createUndoAction(this, tr("&Undo"));
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actUndo->setIcon(stdIcon(QStyle::SP_ArrowBack, QStringLiteral("edit-undo")));
    m_actRedo = m_undoGroup->createRedoAction(this, tr("&Redo"));
    m_actRedo->setShortcuts({QKeySequence::Redo, QKeySequence(tr("Ctrl+Y"))});
    m_actRedo->setIcon(stdIcon(QStyle::SP_ArrowForward, QStringLiteral("edit-redo")));
    m_actAdd = makeAction(this, tr("&Add Item"), QKeySequence(tr("Ctrl+N")), stdIcon(QStyle::SP_FileDialogNewFolder, QStringLiteral("list-add")));
    m_actDuplicate = makeAction(this, tr("&Duplicate Item"), QKeySequence(tr("Ctrl+D")), stdIcon(QStyle::SP_FileLinkIcon, QStringLiteral("edit-copy")));
    m_actDelete = makeAction(this, tr("D&elete Item"), QKeySequence(), stdIcon(QStyle::SP_TrashIcon, QStringLiteral("list-remove")));
    m_actUp = makeAction(this, tr("Move Item &Up"), QKeySequence(tr("Alt+Up")), stdIcon(QStyle::SP_ArrowUp, QStringLiteral("go-up")));
    m_actDown = makeAction(this, tr("Move Item Do&wn"), QKeySequence(tr("Alt+Down")), stdIcon(QStyle::SP_ArrowDown, QStringLiteral("go-down")));
    m_actFind = makeAction(this, tr("&Find Item"), QKeySequence::Find);
    m_actLink = makeAction(this, tr("Change &Class…"), QKeySequence(tr("Ctrl+L")),
                           stdIcon(QStyle::SP_FileDialogDetailedView, QStringLiteral("document-properties")));
    m_actLink->setToolTip(tr("Link the current JSON file to a different class"));
    m_actPoly = makeAction(this, tr("&Polymorphic Types…"), QKeySequence());
    m_actReload = makeAction(this, tr("&Reload Scripts"), QKeySequence(tr("F5")), stdIcon(QStyle::SP_BrowserReload, QStringLiteral("view-refresh")));
    m_actFixAll = makeAction(this, tr("&Fix All Problems…"), QKeySequence(), stdIcon(QStyle::SP_DialogApplyButton));
    m_actSettings = makeAction(this, tr("&Settings…"), QKeySequence::Preferences);
    m_actAbout = makeAction(this, tr("&About GInspectorAdvanced"), QKeySequence());

    connect(m_actOpenProject, &QAction::triggered, this, &MainWindow::openProjectDialog);
    connect(m_actCloseProject, &QAction::triggered, this, &MainWindow::closeProject);
    connect(m_actOpenJson, &QAction::triggered, this, &MainWindow::chooseJson);
    connect(m_actSave, &QAction::triggered, this, &MainWindow::saveCurrent);
    connect(m_actSaveAll, &QAction::triggered, this, &MainWindow::saveAll);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);
    connect(m_actAdd, &QAction::triggered, this, [this] { addItem(QString()); });
    connect(m_actDuplicate, &QAction::triggered, this, &MainWindow::duplicateItem);
    connect(m_actDelete, &QAction::triggered, this, &MainWindow::deleteItem);
    connect(m_actUp, &QAction::triggered, this, [this] { moveItem(-1); });
    connect(m_actDown, &QAction::triggered, this, [this] { moveItem(1); });
    connect(m_actFind, &QAction::triggered, this, [this] { m_search->setFocus(); m_search->selectAll(); });
    connect(m_actLink, &QAction::triggered, this, &MainWindow::changeClass);
    connect(m_actPoly, &QAction::triggered, this, [this] {
        PolymorphismDialog d(m_project, this);
        d.exec();
    });
    connect(m_actReload, &QAction::triggered, this, &MainWindow::reloadScripts);
    connect(m_actFixAll, &QAction::triggered, this, &MainWindow::fixAll);
    connect(m_actSettings, &QAction::triggered, this, [this] {
        SettingsDialog d(m_project, this);
        d.exec();
        m_builtinLabel->setText(m_project->registry().builtinSource());
    });
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actOpenProject);
    m_recentMenu = file->addMenu(tr("Recent Projects"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        for (const QString &p : AppSettings::recentProjects())
            m_recentMenu->addAction(QDir::toNativeSeparators(p), this, [this, p] { openProject(p); });
        if (m_recentMenu->isEmpty())
            m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
    });
    file->addAction(m_actCloseProject);
    file->addSeparator();
    file->addAction(m_actOpenJson);
    file->addAction(m_actSave);
    file->addAction(m_actSaveAll);
    file->addSeparator();
    file->addAction(m_actQuit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actUndo);
    edit->addAction(m_actRedo);
    edit->addSeparator();
    edit->addAction(m_actAdd);
    edit->addAction(m_actDuplicate);
    edit->addAction(m_actDelete);
    edit->addAction(m_actUp);
    edit->addAction(m_actDown);
    edit->addSeparator();
    edit->addAction(m_actFind);

    QMenu *proj = menuBar()->addMenu(tr("&Project"));
    proj->addAction(m_actLink);
    proj->addAction(m_actPoly);
    proj->addAction(m_actReload);
    proj->addSeparator();
    proj->addAction(m_actFixAll);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    for (QDockWidget *d : findChildren<QDockWidget *>())
        view->addAction(d->toggleViewAction());

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(m_actSettings);
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actAbout);

    QToolBar *tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolbar"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(m_actOpenProject);
    tb->addAction(m_actOpenJson);
    tb->addAction(m_actSave);
    tb->addAction(m_actSaveAll);
    tb->addSeparator();
    tb->addAction(m_actUndo);
    tb->addAction(m_actRedo);
    tb->addSeparator();
    tb->addAction(m_actLink);
    tb->addAction(m_actReload);
}

QWidget *MainWindow::buildWelcome()
{
    auto *w = new QWidget(this);
    auto *outer = new QVBoxLayout(w);
    outer->addStretch(1);
    auto *center = new QWidget(w);
    center->setMaximumWidth(520);
    auto *l = new QVBoxLayout(center);
    auto *icon = new QLabel(center);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(128, 128));
    icon->setAlignment(Qt::AlignCenter);
    l->addWidget(icon);
    auto *title = new QLabel(QStringLiteral("GInspectorAdvanced"), center);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 2);
    f.setBold(true);
    title->setFont(f);
    title->setAlignment(Qt::AlignCenter);
    l->addWidget(title);
    auto *sub = new QLabel(tr("Edit the JSON data of your Godot project with an inspector\nthat knows your classes."), center);
    sub->setAlignment(Qt::AlignCenter);
    l->addWidget(sub);
    l->addSpacing(12);
    auto *open = new QPushButton(stdIcon(QStyle::SP_DirOpenIcon, QStringLiteral("folder-open")), tr("Open Godot Project…"), center);
    open->setMinimumHeight(36);
    connect(open, &QPushButton::clicked, this, &MainWindow::openProjectDialog);
    l->addWidget(open);
    l->addSpacing(8);
    l->addWidget(new QLabel(tr("Recent projects"), center));
    m_recent = new QListWidget(center);
    m_recent->setMaximumHeight(180);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        openProject(it->data(Qt::UserRole).toString());
    });
    l->addWidget(m_recent);
    auto *hl = new QHBoxLayout();
    hl->addStretch(1);
    hl->addWidget(center, 3);
    hl->addStretch(1);
    outer->addLayout(hl);
    outer->addStretch(2);
    refreshWelcome();
    return w;
}

void MainWindow::refreshWelcome()
{
    if (!m_recent)
        return;
    m_recent->clear();
    for (const QString &p : AppSettings::recentProjects()) {
        auto *it = new QListWidgetItem(QStringLiteral("%1\n%2").arg(QFileInfo(p).fileName(), QDir::toNativeSeparators(p)), m_recent);
        it->setData(Qt::UserRole, p);
    }
}

void MainWindow::buildUi()
{
    m_stack = new QStackedWidget(this);
    m_welcome = buildWelcome();
    m_stack->addWidget(m_welcome);

    auto *editor = new QSplitter(Qt::Horizontal, m_stack);
    editor->setObjectName(QStringLiteral("editorSplitter"));
    auto *left = new QWidget(editor);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(12, 12, 6, 12);
    ll->setSpacing(Theme::RowSpacing + 2);
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(Theme::FieldSpacing);
    m_docHeader = new QLabel(left);
    m_docHeader->setWordWrap(true);
    m_docHeader->setTextFormat(Qt::RichText);
    headerRow->addWidget(m_docHeader, 1);
    m_changeClassButton = new QPushButton(tr("Change Class…"), left);
    m_changeClassButton->setToolTip(m_actLink->toolTip());
    connect(m_changeClassButton, &QPushButton::clicked, this, &MainWindow::changeClass);
    headerRow->addWidget(m_changeClassButton, 0, Qt::AlignTop);
    ll->addLayout(headerRow);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(tr("Search items…"));
    m_search->setClearButtonEnabled(true);
    ll->addWidget(m_search);

    m_cards = new CardModel(m_project, this);
    m_filter = new CardFilter(this);
    m_filter->setSourceModel(m_cards);
    m_list = new QListView(left);
    m_list->setModel(m_filter);
    m_list->setItemDelegate(new CardDelegate(m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_list->addActions({m_actAdd, m_actDuplicate, m_actDelete, m_actUp, m_actDown});
    m_list->setFrameShape(QFrame::NoFrame);
    ll->addWidget(m_list, 1);

    auto *buttons = new QHBoxLayout();
    m_addButton = new QToolButton(left);
    m_addButton->setDefaultAction(m_actAdd);
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addMenu = new QMenu(m_addButton);
    buttons->addWidget(m_addButton);
    for (QAction *a : {m_actDuplicate, m_actDelete, m_actUp, m_actDown}) {
        auto *b = new QToolButton(left);
        b->setDefaultAction(a);
        buttons->addWidget(b);
    }
    buttons->setSpacing(Theme::FieldSpacing);
    buttons->addStretch(1);
    ll->addLayout(buttons);

    m_inspector = new Inspector(m_project, editor);
    editor->addWidget(left);
    editor->addWidget(m_inspector);
    editor->setStretchFactor(0, 2);
    editor->setStretchFactor(1, 3);
    editor->setSizes({380, 620});
    m_stack->addWidget(editor);
    setCentralWidget(m_stack);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_filter->setFilterRegularExpression(QRegularExpression(QRegularExpression::escape(t)));
        m_filter->setFilterFixedString(t);
    });
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &cur) {
        const int row = cur.isValid() ? m_filter->mapToSource(cur).row() : -1;
        if (row >= 0)
            m_lastRow = row;
        m_inspector->setTarget(m_doc, row);
        updateActions();
    });
    connect(m_inspector, &Inspector::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(m_inspector, &Inspector::changeClassRequested, this, &MainWindow::changeClass);
    connect(m_inspector, &Inspector::plainRequested, this, [this] {
        if (m_doc && linkPlain(m_doc))
            showDocument(m_doc);
    });

    // Data files dock
    auto *filesDock = new QDockWidget(tr("Data Files"), this);
    filesDock->setObjectName(QStringLiteral("filesDock"));
    m_files = new QTreeWidget(filesDock);
    m_files->setHeaderLabels({tr("File"), tr("Class")});
    m_files->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    connect(m_files, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) {
        const QString res = it->data(0, Qt::UserRole).toString();
        if (!res.isEmpty())
            openJson(res);
    });
    connect(m_files, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        const QString res = it->data(0, Qt::UserRole).toString();
        if (!res.isEmpty() && (!m_doc || m_doc->resPath() != res))
            openJson(res);
    });
    filesDock->setWidget(m_files);
    addDockWidget(Qt::LeftDockWidgetArea, filesDock);

    // Problems dock
    auto *probDock = new QDockWidget(tr("Problems"), this);
    probDock->setObjectName(QStringLiteral("problemsDock"));
    auto *pw = new QWidget(probDock);
    auto *pl = new QVBoxLayout(pw);
    pl->setContentsMargins(10, 6, 10, 10);
    pl->setSpacing(Theme::FieldSpacing);
    auto *ph = new QHBoxLayout();
    m_problemSummary = new QLabel(pw);
    ph->addWidget(m_problemSummary, 1);
    m_onlyProblems = new QCheckBox(tr("Show only items with problems"), pw);
    ph->addWidget(m_onlyProblems);
    auto *fix = new QToolButton(pw);
    fix->setDefaultAction(m_actFixAll);
    fix->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ph->addWidget(fix);
    pl->addLayout(ph);
    m_problems = new QTreeWidget(pw);
    m_problems->setHeaderLabels({tr("Item"), tr("Field"), tr("Problem")});
    m_problems->setRootIsDecorated(false);
    m_problems->header()->setStretchLastSection(true);
    pl->addWidget(m_problems, 1);
    probDock->setWidget(pw);
    addDockWidget(Qt::BottomDockWidgetArea, probDock);
    connect(m_onlyProblems, &QCheckBox::toggled, m_filter, &CardFilter::setOnlyProblems);
    connect(m_problems, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) {
        const int item = it->data(0, Qt::UserRole).toInt();
        const QStringList segs = it->data(1, Qt::UserRole).toStringList();
        selectItem(item);
        JPath path;
        for (const QString &s : segs) {
            if (s.startsWith(QLatin1Char('#')))
                path << PathSeg::i(s.mid(1).toInt());
            else
                path << PathSeg::k(s);
        }
        QTimer::singleShot(0, this, [this, path] { m_inspector->reveal(path); });
    });

    m_builtinLabel = new QLabel(m_project->registry().builtinSource(), this);
    m_builtinLabel->setObjectName(QStringLiteral("dimLabel"));
    statusBar()->addPermanentWidget(m_builtinLabel);
    filesDock->hide();
    probDock->hide();
}

void MainWindow::updateTitle()
{
    QString t = QStringLiteral("GInspectorAdvanced");
    if (m_project->isOpen())
        t = m_project->name() + QStringLiteral(" — ") + t;
    if (m_doc)
        t = m_doc->fileName() + (m_doc->isModified() ? QStringLiteral("*") : QString()) + QStringLiteral(" — ") + t;
    setWindowTitle(t);
}

void MainWindow::updateActions()
{
    const bool open = m_project->isOpen();
    const bool linked = m_doc && !m_doc->link().classKey.isEmpty() && m_project->registry().get(m_doc->link().classKey);
    const bool editable = linked || (m_doc && m_doc->isPlain());
    const bool multi = editable && m_doc->shape() != JsonDocument::Shape::Single;
    const int row = currentItem();
    m_actCloseProject->setEnabled(open);
    m_actOpenJson->setEnabled(open);
    m_actSave->setEnabled(m_doc && m_doc->isModified());
    bool anyModified = false;
    for (JsonDocument *d : m_project->documents())
        anyModified |= d->isModified();
    m_actSaveAll->setEnabled(anyModified);
    m_actAdd->setEnabled(multi);
    m_actDuplicate->setEnabled(multi && row >= 0);
    m_actDelete->setEnabled(multi && row >= 0);
    m_actUp->setEnabled(multi && row > 0);
    m_actDown->setEnabled(multi && row >= 0 && row < m_doc->count() - 1);
    m_actLink->setEnabled(m_doc != nullptr);
    m_changeClassButton->setEnabled(m_doc != nullptr);
    m_actPoly->setEnabled(open);
    m_actReload->setEnabled(open);
    m_actFixAll->setEnabled(linked);
    m_actFind->setEnabled(m_doc != nullptr);
}

void MainWindow::updateDocHeader()
{
    if (!m_doc) {
        m_docHeader->clear();
        return;
    }
    const LinkInfo &l = m_doc->link();
    QString cls = l.plain               ? tr("plain JSON")
                  : l.classKey.isEmpty() ? tr("<i>not linked</i>")
                                         : m_project->schema().displayName(l.classKey).toHtmlEscaped();
    if (!l.classKey.isEmpty() && !m_project->registry().get(l.classKey))
        cls += tr(" <span style='color:#e5534b'>(class not found)</span>");
    QString shape;
    switch (m_doc->shape()) {
    case JsonDocument::Shape::Dictionary: shape = tr("dictionary"); break;
    case JsonDocument::Shape::Array: shape = tr("array"); break;
    case JsonDocument::Shape::Single: shape = tr("single object"); break;
    }
    m_docHeader->setText(QStringLiteral("<span style='font-size:large; font-weight:bold'>%1%2</span><br><span style='color:gray'>%3 · %4 · %5</span>")
                             .arg(m_doc->fileName().toHtmlEscaped(), m_doc->isModified() ? QStringLiteral(" *") : QString(),
                                  cls, shape, tr("%n item(s)", nullptr, m_doc->count())));
}

void MainWindow::refreshFileTree()
{
    m_files->clear();
    if (!m_project->isOpen())
        return;
    QHash<QString, QTreeWidgetItem *> dirs;
    const QIcon folder = stdIcon(QStyle::SP_DirIcon, QStringLiteral("folder"));
    const QIcon fileIcon = stdIcon(QStyle::SP_FileIcon, QStringLiteral("text-x-generic"));
    for (const QString &res : m_project->jsonFiles()) {
        const QString rel = res.mid(6);
        const QString dir = rel.contains(QLatin1Char('/')) ? rel.section(QLatin1Char('/'), 0, -2) : QString();
        QTreeWidgetItem *parent = nullptr;
        if (!dir.isEmpty()) {
            parent = dirs.value(dir);
            if (!parent) {
                parent = new QTreeWidgetItem(m_files, {dir + QLatin1Char('/')});
                parent->setIcon(0, folder);
                parent->setExpanded(true);
                dirs.insert(dir, parent);
            }
        }
        auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_files);
        JsonDocument *doc = m_project->document(res);
        const bool modified = doc && doc->isModified();
        it->setText(0, rel.section(QLatin1Char('/'), -1) + (modified ? QStringLiteral(" *") : QString()));
        it->setIcon(0, fileIcon);
        it->setData(0, Qt::UserRole, res);
        it->setToolTip(0, res);
        auto link = m_project->config().links.constFind(res);
        if (link != m_project->config().links.cend()) {
            it->setText(1, link->plain ? tr("Plain JSON") : m_project->schema().displayName(link->classKey));
        } else {
            it->setText(1, tr("—"));
            it->setForeground(1, palette().brush(QPalette::PlaceholderText));
        }
        if (m_doc && m_doc->resPath() == res) {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
            m_files->setCurrentItem(it);
        }
    }
    m_files->sortItems(0, Qt::AscendingOrder);
}

bool MainWindow::openProject(const QString &root)
{
    QList<JsonDocument *> modified;
    for (JsonDocument *d : m_project->documents())
        if (d->isModified())
            modified << d;
    if (!confirmDiscard(modified))
        return false;
    QString err;
    m_doc = nullptr;
    m_cards->setDocument(nullptr);
    m_inspector->setTarget(nullptr, -1);
    for (QUndoStack *s : m_undoGroup->stacks())
        m_undoGroup->removeStack(s);
    if (!m_project->open(root, &err)) {
        QMessageBox::warning(this, tr("Open project"), err);
        AppSettings::removeRecentProject(root);
        refreshWelcome();
        return false;
    }
    AppSettings::addRecentProject(m_project->root());
    refreshWelcome();
    m_stack->setCurrentIndex(1);
    for (QDockWidget *d : findChildren<QDockWidget *>())
        d->show();
    if (AppSettings::windowState(QStringLiteral("state")).isEmpty()) {
        // First run: sensible dock sizes (later runs restore the user's layout).
        if (auto *files = findChild<QDockWidget *>(QStringLiteral("filesDock")))
            resizeDocks({files}, {250}, Qt::Horizontal);
        if (auto *probs = findChild<QDockWidget *>(QStringLiteral("problemsDock")))
            resizeDocks({probs}, {160}, Qt::Vertical);
    }
    refreshFileTree();
    rebuildAddMenu();
    updateTitle();
    updateActions();
    updateDocHeader();
    statusBar()->showMessage(tr("Opened %1: %n script class(es) found.", nullptr,
                                int(m_project->registry().scriptClasses().size()))
                                 .arg(m_project->name()),
                             6000);
    // No JSON file is opened automatically: pick one in the Files panel or with File ▸ Open JSON.
    return true;
}

void MainWindow::openProjectDialog()
{
    const QStringList recent = AppSettings::recentProjects();
    const QString start = recent.isEmpty() ? QDir::homePath() : QFileInfo(recent.first()).absolutePath();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Godot project (folder with project.godot)"), start);
    if (dir.isEmpty())
        return;
    if (!Project::isGodotProject(dir)) {
        QMessageBox::warning(this, tr("Open project"), tr("That folder has no project.godot file."));
        return;
    }
    openProject(dir);
}

void MainWindow::closeProject()
{
    QList<JsonDocument *> modified;
    for (JsonDocument *d : m_project->documents())
        if (d->isModified())
            modified << d;
    if (!confirmDiscard(modified))
        return;
    m_doc = nullptr;
    m_cards->setDocument(nullptr);
    m_inspector->setTarget(nullptr, -1);
    for (QUndoStack *s : m_undoGroup->stacks())
        m_undoGroup->removeStack(s);
    m_project->close();
    m_files->clear();
    m_problems->clear();
    m_stack->setCurrentIndex(0);
    for (QDockWidget *d : findChildren<QDockWidget *>())
        d->hide();
    updateTitle();
    updateActions();
}

void MainWindow::chooseJson()
{
    if (!m_project->isOpen())
        return;
    if (m_project->jsonFiles().isEmpty()) {
        QMessageBox::information(this, tr("Select a JSON file"), tr("This project has no JSON files yet."));
        return;
    }
    JsonPickerDialog d(m_project, this);
    if (d.exec() == QDialog::Accepted && !d.selectedPath().isEmpty())
        openJson(d.selectedPath());
}

void MainWindow::openJson(const QString &resPath)
{
    QString err;
    JsonDocument *doc = m_project->openDocument(resPath, &err);
    if (!doc) {
        QMessageBox::warning(this, tr("Open JSON"), tr("Could not open %1:\n%2\n\nFix the file in a text editor and try again.")
                                                       .arg(resPath, err));
        return;
    }
    if (!m_undoGroup->stacks().contains(doc->undoStack())) {
        m_undoGroup->addStack(doc->undoStack());
        connect(doc, &JsonDocument::modifiedChanged, this, [this] {
            updateTitle();
            updateActions();
            updateDocHeader();
            refreshFileTree();
        });
        connect(doc, &JsonDocument::itemChanged, this, [this] {
            scheduleProblems();
            updateActions();
        });
        connect(doc, &JsonDocument::itemsReset, this, [this, doc] {
            scheduleProblems();
            updateDocHeader();
            if (doc == m_doc && doc->count() > 0)
                QTimer::singleShot(0, this, [this] { selectItem(qMin(m_lastRow, m_doc ? m_doc->count() - 1 : 0)); });
            updateActions();
        });
    }
    if (doc->link().classKey.isEmpty()) {
        if (!linkDocument(doc)) {
            if (!m_doc) // nothing else open: still show the raw file name
                showDocument(doc);
            else
                showDocument(m_doc);
            return;
        }
    }
    showDocument(doc);
}

bool MainWindow::linkDocument(JsonDocument *doc)
{
    ClassPickerDialog d(m_project, doc->resPath(), doc->link(), this);
    if (d.exec() != QDialog::Accepted)
        return false;
    return applyLink(doc, d.link());
}

bool MainWindow::linkPlain(JsonDocument *doc)
{
    LinkInfo l;
    l.plain = true; // shape left empty: detected from the file
    return applyLink(doc, l);
}

bool MainWindow::applyLink(JsonDocument *doc, const LinkInfo &link)
{
    if (doc->isModified()) {
        if (QMessageBox::question(this, tr("Link JSON"), tr("Changing the class discards the unsaved changes of %1. Continue?")
                                                            .arg(doc->fileName()))
            != QMessageBox::Yes)
            return false;
        QString err;
        m_project->reloadDocument(doc, &err);
    }
    QString err;
    if (!m_project->link(doc, link, &err))
        QMessageBox::warning(this, tr("Link JSON"), tr("Could not write %1:\n%2").arg(ProjectConfig::fileName(), err));
    else if (doc->isPlain())
        statusBar()->showMessage(tr("%1 is now edited as plain JSON (saved in %2).").arg(doc->fileName(), ProjectConfig::fileName()),
                                 6000);
    else
        statusBar()->showMessage(tr("%1 is now linked to %2 (saved in %3).")
                                     .arg(doc->fileName(), m_project->schema().displayName(doc->link().classKey),
                                          ProjectConfig::fileName()),
                                 6000);
    return true;
}

void MainWindow::showDocument(JsonDocument *doc)
{
    m_doc = doc;
    m_undoGroup->setActiveStack(doc ? doc->undoStack() : nullptr);
    m_cards->setDocument(doc);
    m_search->clear();
    m_lastRow = 0;
    if (doc) {
        AppSettings::setLastJson(m_project->root(), doc->resPath());
        if (doc->count() > 0)
            selectItem(0);
        else
            m_inspector->setTarget(doc, -1);
    } else {
        m_inspector->setTarget(nullptr, -1);
    }
    rebuildAddMenu();
    refreshFileTree();
    refreshProblems();
    updateDocHeader();
    updateTitle();
    updateActions();
    if (doc)
        QTimer::singleShot(0, this, [this, doc = QPointer<JsonDocument>(doc)] { checkWrongClass(doc); });
}

void MainWindow::changeClass()
{
    if (m_doc && linkDocument(m_doc))
        showDocument(m_doc);
}

void MainWindow::checkWrongClass(JsonDocument *doc)
{
    if (!doc || doc != m_doc || doc->count() == 0 || QApplication::activeModalWidget())
        return;
    const QString cls = doc->link().classKey;
    if (cls.isEmpty() || !m_project->registry().get(cls))
        return;
    const QString key = doc->resPath() + QLatin1Char('|') + cls;
    if (m_wrongClassDismissed.contains(key))
        return;

    Validator val(m_project->schema());
    QString example;
    for (int i = 0; i < doc->count(); ++i) {
        bool failed = false;
        for (const Issue &is : val.validateItem(doc->item(i).value, cls, i, doc->link().keyField)) {
            if (!is.error)
                continue;
            failed = true;
            if (example.isEmpty())
                example = QStringLiteral("%1 › %2: %3")
                              .arg(doc->itemTitle(i), is.path.isEmpty() ? tr("(item)") : pathToString(is.path), is.message);
            break;
        }
        if (!failed)
            return; // at least one item matches the class: it is probably the right one
    }

    const QString clsName = m_project->schema().displayName(cls);
    QMessageBox box(QMessageBox::Question, tr("Wrong class?"),
                    tr("<p><b>None of the %n item(s) in %1 match %2.</b></p>"
                       "<p>Every item has at least one value the class does not accept, for example:</p>"
                       "<p><code>%3</code></p>"
                       "<p>Did you select the wrong class for this file?</p>",
                       nullptr, doc->count())
                        .arg(doc->fileName().toHtmlEscaped(), clsName.toHtmlEscaped(), example.toHtmlEscaped()),
                    QMessageBox::NoButton, this);
    box.setTextFormat(Qt::RichText);
    QPushButton *choose = box.addButton(tr("Choose Another Class…"), QMessageBox::AcceptRole);
    box.addButton(tr("Keep %1").arg(clsName), QMessageBox::RejectRole);
    box.setDefaultButton(choose);
    box.exec();
    if (box.clickedButton() == choose) {
        if (linkDocument(doc)) {
            showDocument(doc); // checks the new class again
            return;
        }
    }
    // Do not ask again for this file and class during this session.
    m_wrongClassDismissed.insert(key);
}

void MainWindow::rebuildAddMenu()
{
    m_addMenu->clear();
    m_addButton->setMenu(nullptr);
    if (!m_doc || m_doc->link().classKey.isEmpty() || !m_project->registry().get(m_doc->link().classKey))
        return;
    const QStringList classes = m_project->schema().assignableClasses(m_doc->link().classKey);
    if (classes.size() < 2)
        return;
    for (const QString &c : classes)
        m_addMenu->addAction(m_project->schema().displayName(c), this, [this, c] { addItem(c); });
    m_addButton->setMenu(m_addMenu);
    m_addButton->setPopupMode(QToolButton::MenuButtonPopup);
}

void MainWindow::scheduleProblems()
{
    m_problemTimer->start();
}

void MainWindow::refreshProblems()
{
    m_problems->clear();
    if (!m_doc || m_doc->link().classKey.isEmpty() || !m_project->registry().get(m_doc->link().classKey)) {
        QString text;
        if (m_doc)
            text = m_doc->isPlain() ? tr("Plain JSON: no class is linked, so nothing is checked.")
                                    : tr("Link the file to a class to validate it.");
        m_problemSummary->setText(text);
        if (auto *dock = qobject_cast<QDockWidget *>(m_problems->parentWidget()->parentWidget()))
            dock->setWindowTitle(tr("Problems"));
        return;
    }
    Validator val(m_project->schema());
    int errors = 0, warnings = 0;
    const QIcon errIcon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
    const QIcon warnIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    for (int i = 0; i < m_doc->count(); ++i) {
        for (const Issue &is : val.validateItem(m_doc->item(i).value, m_doc->link().classKey, i, m_doc->link().keyField)) {
            auto *it = new QTreeWidgetItem(m_problems);
            it->setIcon(0, is.error ? errIcon : warnIcon);
            it->setText(0, m_doc->itemTitle(i));
            it->setData(0, Qt::UserRole, i);
            it->setText(1, pathToString(is.path));
            QStringList segs;
            for (const PathSeg &s : is.path)
                segs << (s.isIndex() ? QStringLiteral("#%1").arg(s.index) : s.key);
            it->setData(1, Qt::UserRole, segs);
            it->setText(2, is.message);
            (is.error ? errors : warnings)++;
            if (m_problems->topLevelItemCount() > 2000)
                break;
        }
    }
    m_problems->resizeColumnToContents(0);
    m_problems->resizeColumnToContents(1);
    if (errors == 0 && warnings == 0)
        m_problemSummary->setText(tr("✔ No problems: every value matches %1.").arg(m_project->schema().displayName(m_doc->link().classKey)));
    else
        m_problemSummary->setText(tr("%1 error(s), %2 warning(s)").arg(errors).arg(warnings));
    if (auto *dock = qobject_cast<QDockWidget *>(m_problems->parentWidget()->parentWidget()))
        dock->setWindowTitle(errors ? tr("Problems (%1)").arg(errors) : tr("Problems"));
}

bool MainWindow::save(JsonDocument *doc)
{
    if (!doc)
        return false;
    if (!doc->link().classKey.isEmpty() && m_project->registry().get(doc->link().classKey)) {
        Validator val(m_project->schema());
        int errors = 0;
        for (int i = 0; i < doc->count(); ++i)
            for (const Issue &is : val.validateItem(doc->item(i).value, doc->link().classKey, i, doc->link().keyField))
                errors += is.error ? 1 : 0;
        if (errors > 0) {
            QMessageBox box(QMessageBox::Warning, tr("Save"),
                            tr("%1 still has %n problem(s) that do not match its class.", nullptr, errors).arg(doc->fileName()),
                            QMessageBox::NoButton, this);
            QPushButton *fixBtn = box.addButton(tr("Fix and Save"), QMessageBox::AcceptRole);
            QPushButton *saveBtn = box.addButton(tr("Save Anyway"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == fixBtn) {
                JsonDocument *keep = m_doc;
                m_doc = doc;
                fixAll();
                m_doc = keep;
            } else if (box.clickedButton() != saveBtn) {
                return false;
            }
        }
    }
    QString err;
    if (!doc->save(m_project->config().backups, &err)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(doc->fileName(), err));
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(doc->resPath()), 4000);
    updateTitle();
    updateActions();
    updateDocHeader();
    refreshFileTree();
    m_list->viewport()->update();
    return true;
}

void MainWindow::saveCurrent()
{
    save(m_doc);
}

void MainWindow::saveAll()
{
    for (JsonDocument *d : m_project->documents())
        if (d->isModified() && !save(d))
            return;
}

bool MainWindow::confirmDiscard(const QList<JsonDocument *> &docs)
{
    if (docs.isEmpty())
        return true;
    QStringList names;
    for (JsonDocument *d : docs)
        names << d->resPath();
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"),
                                        tr("These files have unsaved changes:\n\n%1\n\nSave them?").arg(names.join(QLatin1Char('\n'))),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        for (JsonDocument *d : docs)
            if (!save(d))
                return false;
    return true;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QList<JsonDocument *> modified;
    for (JsonDocument *d : m_project->documents())
        if (d->isModified())
            modified << d;
    if (!confirmDiscard(modified)) {
        e->ignore();
        return;
    }
    AppSettings::setWindowState(QStringLiteral("geometry"), saveGeometry());
    AppSettings::setWindowState(QStringLiteral("state"), saveState());
    e->accept();
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && isActiveWindow())
        QTimer::singleShot(200, this, &MainWindow::checkExternalChanges);
}

void MainWindow::checkExternalChanges()
{
    if (m_checkingExternal || !m_project->isOpen() || QApplication::activeModalWidget())
        return;
    m_checkingExternal = true;
    if (m_project->scriptsChanged()) {
        reloadScripts();
        statusBar()->showMessage(tr("Scripts changed on disk: classes reloaded."), 5000);
    }
    for (JsonDocument *d : m_project->documents()) {
        if (!d->changedOnDisk())
            continue;
        bool reload = !d->isModified();
        if (!reload)
            reload = QMessageBox::question(this, tr("File changed"),
                                           tr("%1 was changed outside GInspectorAdvanced.\nReload it and lose your unsaved changes?")
                                               .arg(d->resPath()))
                     == QMessageBox::Yes;
        QString err;
        if (reload) {
            if (!m_project->reloadDocument(d, &err))
                QMessageBox::warning(this, tr("Reload"), err);
            else
                statusBar()->showMessage(tr("Reloaded %1").arg(d->resPath()), 4000);
        } else {
            // Do not ask again for the same change.
            d->load(&err);
        }
        if (d == m_doc)
            showDocument(d);
    }
    m_checkingExternal = false;
}

void MainWindow::reloadScripts()
{
    m_project->reloadScripts();
    for (JsonDocument *d : m_project->documents())
        if (!d->link().classKey.isEmpty())
            d->setLink(d->link());
    updateDocHeader();
    statusBar()->showMessage(tr("Reloaded %n script class(es).", nullptr, int(m_project->registry().scriptClasses().size())), 4000);
}

int MainWindow::currentItem() const
{
    const QModelIndex i = m_list->currentIndex();
    return i.isValid() ? m_filter->mapToSource(i).row() : -1;
}

void MainWindow::selectItem(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->count())
        return;
    QModelIndex pi = m_filter->mapFromSource(m_cards->index(index));
    if (!pi.isValid()) {
        m_search->clear();
        m_onlyProblems->setChecked(false);
        pi = m_filter->mapFromSource(m_cards->index(index));
    }
    m_list->setCurrentIndex(pi);
    m_list->scrollTo(pi);
}

void MainWindow::addItem(const QString &classKeyIn)
{
    if (!m_doc || m_doc->shape() == JsonDocument::Shape::Single)
        return;
    const QString declared = m_doc->link().classKey;
    const QString cls = classKeyIn.isEmpty() ? declared : classKeyIn;
    QList<DocItem> items = m_doc->items();
    DocItem it;
    if (m_doc->isPlain()) {
        // Plain JSON: a new empty object, or an empty value like the other items when they are not objects.
        const JValue &like = items.isEmpty() ? JValue::object() : items[currentItem() >= 0 ? currentItem() : items.size() - 1].value;
        it.value = like.isArray() ? JValue::array() : like.isString() ? JValue(QString())
                   : like.isNumber()                ? JValue(0)
                   : like.isBool()                  ? JValue(false)
                   : like.isNull()                  ? JValue::null()
                                                    : JValue::object();
    } else {
        it.value = m_project->schema().newObject(cls, declared);
    }
    it.value.setLayout(JValue::LayoutMultiline);
    if (m_doc->shape() == JsonDocument::Shape::Dictionary)
        it.key = m_doc->uniqueKey(QStringLiteral("new_item"));
    const int at = currentItem() >= 0 ? currentItem() + 1 : items.size();
    items.insert(at, it);
    m_lastRow = at;
    m_doc->undoStack()->push(new ItemsCommand(m_doc, items, tr("Add item")));
}

void MainWindow::duplicateItem()
{
    const int row = currentItem();
    if (!m_doc || row < 0)
        return;
    QList<DocItem> items = m_doc->items();
    DocItem copy = items[row];
    if (m_doc->shape() == JsonDocument::Shape::Dictionary)
        copy.key = m_doc->uniqueKey(copy.key);
    items.insert(row + 1, copy);
    m_lastRow = row + 1;
    m_doc->undoStack()->push(new ItemsCommand(m_doc, items, tr("Duplicate %1").arg(m_doc->itemTitle(row))));
}

void MainWindow::deleteItem()
{
    const int row = currentItem();
    if (!m_doc || row < 0)
        return;
    QList<DocItem> items = m_doc->items();
    const QString title = m_doc->itemTitle(row);
    items.removeAt(row);
    m_lastRow = qMax(0, row - (row >= items.size() ? 1 : 0));
    m_doc->undoStack()->push(new ItemsCommand(m_doc, items, tr("Delete %1").arg(title)));
    statusBar()->showMessage(tr("Deleted %1 (Ctrl+Z to undo)").arg(title), 5000);
}

void MainWindow::moveItem(int delta)
{
    const int row = currentItem();
    if (!m_doc || row < 0)
        return;
    const int to = row + delta;
    if (to < 0 || to >= m_doc->count())
        return;
    QList<DocItem> items = m_doc->items();
    items.move(row, to);
    m_lastRow = to;
    m_doc->undoStack()->push(new ItemsCommand(m_doc, items, tr("Move item")));
}

void MainWindow::fixAll()
{
    if (!m_doc || m_doc->link().classKey.isEmpty())
        return;
    Validator val(m_project->schema());
    QList<DocItem> items = m_doc->items();
    int changed = 0;
    int errors = 0;
    for (int i = 0; i < items.size(); ++i) {
        for (const Issue &is : val.validateItem(items[i].value, m_doc->link().classKey, i, m_doc->link().keyField))
            errors += is.error ? 1 : 0;
        const JValue fixed = val.sanitizeObject(items[i].value, m_doc->link().classKey, m_doc->link().keyField);
        if (!(fixed == items[i].value) || fixed.keys() != items[i].value.keys()) {
            items[i].value = fixed;
            ++changed;
        }
    }
    if (changed == 0) {
        statusBar()->showMessage(tr("Nothing to fix."), 4000);
        return;
    }
    const auto r = QMessageBox::question(
        this, tr("Fix all problems"),
        tr("%n item(s) will be repaired:\n\n• fields the class does not declare are removed\n"
           "• numbers outside their range are clamped\n• values of the wrong type are removed so the class default applies\n\n"
           "You can undo this with Ctrl+Z.",
           nullptr, changed));
    if (r != QMessageBox::Yes)
        return;
    m_doc->undoStack()->push(new ItemsCommand(m_doc, items, tr("Fix all problems")));
    Q_UNUSED(errors);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About GInspectorAdvanced"),
                       tr("<h3>GInspectorAdvanced %1</h3>"
                          "<p>A visual inspector for the JSON data files of Godot projects. Each JSON file is linked to a "
                          "GDScript, C# or built-in class, and only that class' variables, types and restrictions are allowed.</p>"
                          "<p>Built with Qt %2.</p>"
                          "<p>The icon is inspired by the Godot Engine logo by Andrea Calabró (CC BY 4.0).</p>")
                           .arg(QStringLiteral(GIA_VERSION), QString::fromLatin1(qVersion())));
}
