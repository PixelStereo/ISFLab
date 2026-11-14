#include "MainWindow.h"
#include "AppStyle.h"
#include "ShaderPreview.h"
#include "ShaderCheckWorker.h"
#include <cstring>
#include <QApplication>
#include <QLocale>
#include <QIcon>

#ifdef Q_OS_MACOS
void applyMacApplicationIcon();
#endif

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], ShaderCheckWorkerFlag) == 0) return runShaderCheckWorker(argc, argv);
    QLocale::setDefault(QLocale(QLocale::English));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    ShaderPreview::configureDefaultFormat();
    QApplication app(argc, argv);
    app.setApplicationName("ISFLab");
    app.setApplicationVersion(ISFLAB_VERSION);
    app.setOrganizationName("Pixel Stereo");
    app.setDesktopFileName("studio.pixelstereo.isflab");
#ifndef Q_OS_MACOS
    app.setWindowIcon(QIcon(":/assets/ISFLab-rounded.png"));
#endif

    if (!app.primaryScreen()) {
        qCritical("ISFLab requires a graphical desktop session.");
        return 1;
    }
    AppStyle::apply(app);
    MainWindow window;
    window.show();
#ifdef Q_OS_MACOS
    applyMacApplicationIcon();
#endif
    return app.exec();
}
