#pragma once

#include "model/ProjectConfig.h"

#include <QDialog>

class Project;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QTableWidget;
class QListWidget;

// "Select a JSON file": every JSON file of the project with the class it is linked to.
class JsonPickerDialog : public QDialog
{
    Q_OBJECT
public:
    JsonPickerDialog(Project *project, QWidget *parent = nullptr);
    QString selectedPath() const { return m_selected; }

private:
    void fill();
    Project *m_project;
    QLineEdit *m_search;
    QTreeWidget *m_tree;
    QString m_selected;
};

// "Select a class": project scripts, Godot built-ins, or any script file,
// plus how the items are stored in the JSON file.
class ClassPickerDialog : public QDialog
{
    Q_OBJECT
public:
    ClassPickerDialog(Project *project, const QString &jsonResPath, const LinkInfo &current, QWidget *parent = nullptr);
    LinkInfo link() const;

private:
    void fill();
    void updateDetails();
    void browseScript();
    void selectKey(const QString &key);
    QString currentKey() const;

    Project *m_project;
    QString m_json;
    QLineEdit *m_search;
    QTreeWidget *m_tree;
    QTreeWidget *m_fields;
    QLabel *m_info;
    QComboBox *m_shape;
    QComboBox *m_keyField;
    QString m_initialKeyField;
    QString m_detectedShape;
    bool m_plain = false;
};

// Application and project settings.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(Project *project, QWidget *parent = nullptr);
    void apply();
    bool projectSettingsChanged() const { return m_projectChanged; }

private:
    void generateApi();
    void updateApiLabel();

    Project *m_project;
    QComboBox *m_vector;
    QComboBox *m_color;
    QComboBox *m_scope;
    QCheckBox *m_backups;
    QLineEdit *m_godot;
    QLabel *m_apiLabel;
    bool m_projectChanged = false;
};

// Edits how subclasses are distinguished inside JSON objects.
class PolymorphismDialog : public QDialog
{
    Q_OBJECT
public:
    PolymorphismDialog(Project *project, QWidget *parent = nullptr);

private:
    void loadBase(const QString &key);
    void storeCurrent();
    void autoFill();
    void save();

    Project *m_project;
    QListWidget *m_bases;
    QCheckBox *m_explicit;
    QLineEdit *m_key;
    QTableWidget *m_table;
    QString m_current;
    QMap<QString, PolyConfig> m_edit;
};
