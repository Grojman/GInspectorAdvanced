#include "core/Resources.h"
#include "model/Project.h"
#include "ui/AppSettings.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("GInspectorAdvanced"));
    QApplication::setApplicationName(QStringLiteral("GInspectorAdvanced"));
    QApplication::setApplicationVersion(QStringLiteral(GIA_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("ginspectoradvanced"));
    giaInitResources();
    Theme::apply(app);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Visual inspector for Godot JSON data files"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Godot project folder to open"));
    parser.process(app);

    MainWindow w;
    w.show();
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        const QString dir = QDir(args.first()).absolutePath();
        QTimer::singleShot(0, &w, [&w, dir] { w.openProject(dir); });
    }
    return app.exec();
}
