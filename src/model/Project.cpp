#include "Project.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUndoStack>

Project::Project(QObject *parent) : QObject(parent), m_schema(std::make_unique<Schema>(&m_registry, &m_config))
{
}

Project::~Project()
{
    qDeleteAll(m_docs);
}

bool Project::isGodotProject(const QString &dir)
{
    return QFileInfo::exists(QDir(dir).filePath(QStringLiteral("project.godot")));
}

bool Project::open(const QString &rootIn, QString *error)
{
    const QString root = QDir(rootIn).absolutePath();
    if (!isGodotProject(root)) {
        if (error)
            *error = QStringLiteral("%1 has no project.godot file.").arg(QDir::toNativeSeparators(root));
        return false;
    }
    close();
    QString cfgError;
    if (!m_config.load(root, &cfgError)) {
        if (error)
            *error = QStringLiteral("Could not read %1: %2").arg(ProjectConfig::fileName(), cfgError);
        return false;
    }
    m_root = root;
    m_name = QFileInfo(root).fileName();
    QFile pf(QDir(root).filePath(QStringLiteral("project.godot")));
    if (pf.open(QIODevice::ReadOnly)) {
        static const QRegularExpression re(QStringLiteral("^config/name=\"(.*)\"\\s*$"), QRegularExpression::MultilineOption);
        const auto m = re.match(QString::fromUtf8(pf.readAll()));
        if (m.hasMatch())
            m_name = m.captured(1);
    }
    m_registry.setProjectRoot(root);
    m_registry.setFormat(m_config.format);
    m_registry.setIncludePrivate(m_config.includePrivate);
    m_registry.scanProject();
    emit opened();
    return true;
}

void Project::close()
{
    if (!isOpen())
        return;
    qDeleteAll(m_docs);
    m_docs.clear();
    m_root.clear();
    m_name.clear();
    m_registry.setProjectRoot(QString());
    m_registry.scanProject();
    emit closed();
}

QStringList Project::jsonFiles() const
{
    QStringList out;
    if (!isOpen())
        return out;
    QDirIterator it(m_root, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
    const QDir root(m_root);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir::fromNativeSeparators(root.relativeFilePath(path));
        if (rel.startsWith(QLatin1Char('.')) || rel.contains(QLatin1String("/.")))
            continue; // .godot, .git, hidden folders
        if (rel == ProjectConfig::fileName())
            continue;
        if (rel.startsWith(QLatin1String("addons/")) && !m_config.links.contains(QStringLiteral("res://") + rel))
            continue; // plugin internals, unless explicitly linked
        out << QStringLiteral("res://") + rel;
    }
    std::sort(out.begin(), out.end());
    return out;
}

JsonDocument *Project::document(const QString &resPath) const
{
    for (JsonDocument *d : m_docs)
        if (d->resPath() == resPath)
            return d;
    return nullptr;
}

QList<JsonDocument *> Project::documents() const
{
    return m_docs;
}

QString Project::resolveLinkClass(const LinkInfo &link)
{
    if (m_registry.get(link.classKey))
        return link.classKey;
    if (!link.scriptPath.isEmpty()) {
        const QString abs = m_registry.toAbs(link.scriptPath);
        if (QFileInfo::exists(abs)) {
            const QString key = m_registry.addScript(abs);
            if (m_registry.get(link.classKey))
                return link.classKey;
            return key;
        }
    }
    return QString();
}

QStringList Project::classFields(const QString &classKey) const
{
    QStringList out;
    if (classKey.isEmpty())
        return out;
    for (const ResolvedProperty &p : m_registry.properties(classKey))
        out << p.info.name;
    return out;
}

JsonDocument *Project::openDocument(const QString &resPath, QString *error)
{
    if (JsonDocument *d = document(resPath))
        return d;
    auto *doc = new JsonDocument(m_registry.toAbs(resPath), resPath);
    if (!doc->load(error)) {
        delete doc;
        return nullptr;
    }
    m_docs << doc;
    auto it = m_config.links.constFind(resPath);
    if (it != m_config.links.cend()) {
        LinkInfo l = it.value();
        const QString key = resolveLinkClass(l);
        if (!key.isEmpty())
            l.classKey = key;
        doc->applyLink(l, classFields(l.classKey));
    }
    emit documentOpened(doc);
    return doc;
}

void Project::closeDocument(JsonDocument *doc)
{
    if (m_docs.removeOne(doc))
        doc->deleteLater();
}

bool Project::reloadDocument(JsonDocument *doc, QString *error)
{
    if (!doc->load(error))
        return false;
    LinkInfo l = doc->link();
    auto it = m_config.links.constFind(doc->resPath());
    if (it != m_config.links.cend())
        l = it.value();
    doc->applyLink(l, classFields(l.classKey));
    doc->undoStack()->clear();
    return true;
}

bool Project::link(JsonDocument *doc, const LinkInfo &linkIn, QString *error)
{
    LinkInfo l = linkIn;
    l.json = doc->resPath();
    doc->applyLink(l, classFields(l.classKey));
    l.shape = doc->link().shape;
    m_config.links.insert(l.json, l);
    doc->setLink(l);
    doc->undoStack()->clear();
    emit linksChanged();
    return saveConfig(error);
}

bool Project::saveConfig(QString *error)
{
    return m_config.save(error);
}

void Project::reloadScripts()
{
    m_registry.setFormat(m_config.format);
    m_registry.setIncludePrivate(m_config.includePrivate);
    m_registry.scanProject();
    // Scripts linked from outside the project are re-added on demand.
    for (JsonDocument *d : std::as_const(m_docs))
        if (!d->link().classKey.isEmpty())
            resolveLinkClass(d->link());
    emit schemaChanged();
}

QString Project::loadBuiltins(const QString &extensionApiPath)
{
    const QString src = m_registry.loadBuiltins(extensionApiPath);
    m_registry.scanProject(); // re-resolve bases against the new builtin set
    emit schemaChanged();
    return src;
}
