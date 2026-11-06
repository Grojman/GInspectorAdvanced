#pragma once

#include <QByteArray>
#include <QStringList>

// Per-user application settings (stored with QSettings, not in the project).
namespace AppSettings {

QString godotExecutable();
void setGodotExecutable(const QString &path);
QString extensionApiPath();
void setExtensionApiPath(const QString &path);
QStringList recentProjects();
void addRecentProject(const QString &path);
void removeRecentProject(const QString &path);
QByteArray windowState(const QString &key);
void setWindowState(const QString &key, const QByteArray &state);
QString lastJson(const QString &projectRoot);
void setLastJson(const QString &projectRoot, const QString &res);

} // namespace AppSettings
