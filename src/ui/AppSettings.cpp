#include "AppSettings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSettings>

namespace AppSettings {

QString godotExecutable()
{
    return QSettings().value(QStringLiteral("godot/executable")).toString();
}

void setGodotExecutable(const QString &path)
{
    QSettings().setValue(QStringLiteral("godot/executable"), path);
}

QString extensionApiPath()
{
    return QSettings().value(QStringLiteral("godot/extensionApi")).toString();
}

void setExtensionApiPath(const QString &path)
{
    QSettings().setValue(QStringLiteral("godot/extensionApi"), path);
}

QStringList recentProjects()
{
    QStringList out;
    for (const QString &p : QSettings().value(QStringLiteral("recentProjects")).toStringList())
        if (QDir(p).exists())
            out << p;
    return out;
}

void addRecentProject(const QString &path)
{
    QStringList l = QSettings().value(QStringLiteral("recentProjects")).toStringList();
    l.removeAll(path);
    l.prepend(path);
    while (l.size() > 10)
        l.removeLast();
    QSettings().setValue(QStringLiteral("recentProjects"), l);
}

void removeRecentProject(const QString &path)
{
    QStringList l = QSettings().value(QStringLiteral("recentProjects")).toStringList();
    l.removeAll(path);
    QSettings().setValue(QStringLiteral("recentProjects"), l);
}

QByteArray windowState(const QString &key)
{
    return QSettings().value(QStringLiteral("window/") + key).toByteArray();
}

void setWindowState(const QString &key, const QByteArray &state)
{
    QSettings().setValue(QStringLiteral("window/") + key, state);
}

static QString projectKey(const QString &root)
{
    return QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString lastJson(const QString &projectRoot)
{
    return QSettings().value(QStringLiteral("lastJson/") + projectKey(projectRoot)).toString();
}

void setLastJson(const QString &projectRoot, const QString &res)
{
    QSettings().setValue(QStringLiteral("lastJson/") + projectKey(projectRoot), res);
}

} // namespace AppSettings
