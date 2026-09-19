#include "editortheme.h"
#include "editorlanguage.h"
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QScreen>
#include <private/qhighdpiscaling_p.h>
#include <qpa/qplatformscreen.h>

static void updateScreenScaling(QScreen *screen)
{
    if (!screen->handle())
        return;
    // QScreen's logical DPI is already normalized when scaling is active.
    // Read the Windows backend's original DPI to retain fractional factors.
    const qreal factor = screen->handle()->logicalDpi().first / 96.0;
    if (factor > 0 && !qFuzzyCompare(QHighDpiScaling::factor(screen), factor))
        QHighDpiScaling::setScreenFactor(screen, factor);
    if (QGuiApplication::primaryScreen())
        QHighDpiScaling::updateHighDpiScaling();
}

static void followScreenScaling(QScreen *screen)
{
    updateScreenScaling(screen);
    QObject::connect(screen, &QScreen::logicalDotsPerInchChanged, screen,
        [screen] { updateScreenScaling(screen); });
}

int main(int argc, char *argv[])
{
    // Qt 5.6 rounds Windows' automatic scale to an integer. Use its per-screen
    // scaling API instead; do not disable high-DPI initialization itself.
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    if (!qEnvironmentVariableIsSet("QT_SCALE_FACTOR") && !qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS")) {
        for (QScreen *screen : application.screens())
            followScreenScaling(screen);
        QObject::connect(&application, &QGuiApplication::screenAdded, &application, followScreenScaling);
    }
    application.setApplicationName(QStringLiteral("QtGMS"));
    application.setOrganizationName(QStringLiteral("QtGMS"));
    application.setApplicationVersion(QStringLiteral("0.2.5"));
    application.setWindowIcon(QIcon(QStringLiteral(":/images/application.ico")));
    EditorLanguage::install(application);
    EditorTheme::apply(application);

    MainWindow mainWindow(application.arguments().value(1));
    mainWindow.show();
    return application.exec();
}
