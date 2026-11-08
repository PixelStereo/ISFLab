#include "MainWindow.h"
#include "AppStyle.h"
#include <QApplication>
#include <QLocale>
#include <QIcon>

int main(int argc, char *argv[]) {
    QLocale::setDefault(QLocale(QLocale::English));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationName("ISFLab");
    app.setApplicationVersion(ISFLAB_VERSION);
    app.setOrganizationName("Pixel Stereo");
    app.setDesktopFileName("studio.pixelstereo.isflab");
    app.setWindowIcon(QIcon(":/assets/ISFLab.png"));

    if (!app.primaryScreen()) {
        qCritical("ISFLab requires a graphical desktop session.");
        return 1;
    }
    AppStyle::apply(app);
    MainWindow window;
    window.show();
    return app.exec();
}
