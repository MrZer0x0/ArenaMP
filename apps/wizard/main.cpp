#include <QApplication>
#include <QDir>

#include <components/misc/arenarussiantranslator.hpp>
#include <components/misc/arenatheme.hpp>
#include <components/misc/arenaglasswindow.hpp>

#include "mainwizard.hpp"

#ifdef MAC_OS_X_VERSION_MIN_REQUIRED
#undef MAC_OS_X_VERSION_MIN_REQUIRED
// We need to do this because of Qt: https://bugreports.qt-project.org/browse/QTBUG-22154
#define MAC_OS_X_VERSION_MIN_REQUIRED __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

int main(int argc, char *argv[])
{

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ArenaMP Setup Wizard"));

    ArenaUi::applyMorrowindGlassPalette(app);

    // Use Russian only on a Russian Windows/system locale. All other
    // locales deliberately stay English to avoid partial/mixed UI.
    ArenaUi::RussianTranslator arenaTranslator;
    if (ArenaUi::useRussianSystemUi())
        app.installTranslator(&arenaTranslator);

    // Now we make sure the current dir is set to application path
    QDir dir(QCoreApplication::applicationDirPath());

    #ifdef Q_OS_MAC
    // force Qt to load only LOCAL plugins, don't touch system Qt installation
    QDir pluginsPath(QCoreApplication::applicationDirPath());
    pluginsPath.cdUp();
    pluginsPath.cd("Plugins");

    QStringList libraryPaths;
    libraryPaths << pluginsPath.path() << QCoreApplication::applicationDirPath();
    app.setLibraryPaths(libraryPaths);
    #endif

    QDir::setCurrent(dir.absolutePath());

    Wizard::MainWizard wizard;
    ArenaUi::installGlassWindow(wizard);

    wizard.show();
    return app.exec();
}
