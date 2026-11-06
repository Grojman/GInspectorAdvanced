#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QSet>

class CardFilter;
class CardModel;
class Inspector;
class JsonDocument;
struct LinkInfo;
class Project;
class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPushButton;
class QMenu;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QUndoGroup;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openProject(const QString &root);
    void openProjectDialog();

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void buildActions();
    void buildMenus();
    void buildUi();
    QWidget *buildWelcome();
    void refreshWelcome();
    void refreshFileTree();
    void scheduleProblems();
    void refreshProblems();
    void updateTitle();
    void updateActions();
    void updateDocHeader();

    void chooseJson();
    void openJson(const QString &resPath);
    // Asks for the class of a document (or plain JSON) and links it.
    bool linkDocument(JsonDocument *doc);
    // Links a document as plain JSON, without a class.
    bool linkPlain(JsonDocument *doc);
    bool applyLink(JsonDocument *doc, const LinkInfo &link);
    void showDocument(JsonDocument *doc);
    void closeProject();

    bool save(JsonDocument *doc);
    void saveCurrent();
    void saveAll();
    bool confirmDiscard(const QList<JsonDocument *> &docs);

    int currentItem() const;
    void selectItem(int index);
    void addItem(const QString &classKey);
    void duplicateItem();
    void deleteItem();
    void moveItem(int delta);
    void fixAll();
    void reloadScripts();
    void checkExternalChanges();
    void rebuildAddMenu();
    void showAbout();
    void changeClass();
    // Asks whether the class is wrong when no item of the file matches it.
    void checkWrongClass(JsonDocument *doc);

    Project *m_project;
    QPointer<JsonDocument> m_doc;
    QUndoGroup *m_undoGroup;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_welcome = nullptr;
    QListWidget *m_recent = nullptr;
    Inspector *m_inspector = nullptr;
    CardModel *m_cards = nullptr;
    CardFilter *m_filter = nullptr;
    QListView *m_list = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_docHeader = nullptr;
    QPushButton *m_changeClassButton = nullptr;
    QSet<QString> m_wrongClassDismissed;
    QTreeWidget *m_files = nullptr;
    QTreeWidget *m_problems = nullptr;
    QLabel *m_problemSummary = nullptr;
    QCheckBox *m_onlyProblems = nullptr;
    QLabel *m_builtinLabel = nullptr;
    QToolButton *m_addButton = nullptr;
    QMenu *m_addMenu = nullptr;
    QMenu *m_recentMenu = nullptr;
    QTimer *m_problemTimer = nullptr;
    int m_lastRow = 0;
    bool m_checkingExternal = false;

    QAction *m_actOpenProject, *m_actCloseProject, *m_actOpenJson, *m_actSave, *m_actSaveAll, *m_actQuit;
    QAction *m_actUndo, *m_actRedo, *m_actAdd, *m_actDuplicate, *m_actDelete, *m_actUp, *m_actDown;
    QAction *m_actLink, *m_actPoly, *m_actReload, *m_actFixAll, *m_actSettings, *m_actAbout, *m_actFind;
};
