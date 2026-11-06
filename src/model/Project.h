#pragma once

#include "ClassRegistry.h"
#include "JsonDocument.h"
#include "ProjectConfig.h"
#include "Schema.h"

#include <QObject>
#include <memory>

// An opened Godot project: its classes, its ginspector.json and the JSON
// documents being edited.
class Project : public QObject
{
    Q_OBJECT
public:
    explicit Project(QObject *parent = nullptr);
    ~Project() override;

    static bool isGodotProject(const QString &dir);

    bool open(const QString &root, QString *error);
    void close();
    bool isOpen() const { return !m_root.isEmpty(); }
    QString root() const { return m_root; }
    QString name() const { return m_name; }

    ClassRegistry &registry() { return m_registry; }
    ProjectConfig &config() { return m_config; }
    Schema &schema() { return *m_schema; }

    // Every JSON file of the project (res:// paths), except ginspector.json and backups.
    QStringList jsonFiles() const;
    QString toRes(const QString &abs) const { return m_registry.toRes(abs); }
    QString toAbs(const QString &res) const { return m_registry.toAbs(res); }

    JsonDocument *document(const QString &resPath) const;
    QList<JsonDocument *> documents() const;
    // Loads the file (if not loaded yet). If it is linked, items are split by its class.
    JsonDocument *openDocument(const QString &resPath, QString *error);
    void closeDocument(JsonDocument *doc);
    // Re-reads a document from disk (discarding unsaved changes).
    bool reloadDocument(JsonDocument *doc, QString *error);

    bool isLinked(const QString &resPath) const { return m_config.links.contains(resPath); }
    // Links a document to a class and saves ginspector.json.
    bool link(JsonDocument *doc, const LinkInfo &link, QString *error);
    // The class key of a link, re-parsing the script if needed. Empty if not found.
    QString resolveLinkClass(const LinkInfo &link);
    QStringList classFields(const QString &classKey) const;

    bool saveConfig(QString *error);
    // Applies format/private-var settings and re-scans scripts.
    void reloadScripts();
    bool scriptsChanged() const { return m_registry.scriptsChanged(); }
    QString loadBuiltins(const QString &extensionApiPath);

signals:
    void opened();
    void closed();
    void schemaChanged();
    void documentOpened(JsonDocument *doc);
    void linksChanged();

private:
    QString m_root;
    QString m_name;
    ClassRegistry m_registry;
    ProjectConfig m_config;
    std::unique_ptr<Schema> m_schema;
    QList<JsonDocument *> m_docs;
};
