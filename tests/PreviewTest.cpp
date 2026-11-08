#include "ISFScanner.h"
#include "ShaderPreview.h"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (!app.primaryScreen()) return 77;
    ShaderPreview preview;
    preview.resize(320, 180);
    preview.setPlaying(false);
    QString status;
    QObject::connect(&preview, &ShaderPreview::status, &app, [&](const QString &text) { status = text; });
    preview.show();
    QTimer::singleShot(250, &app, [&] {
        for (const QString name : {"ColorWave", "Invert", "Crossfade"}) {
            const auto path = QString::fromUtf8(ISFLAB_EXAMPLES) + "/" + name + ".fs";
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) qFatal("Example unavailable");
            const auto source = file.readAll();
            preview.load(source, *ISFScanner::header(source), path);
            if (name == "Crossfade") preview.setValue("progress", 0.5);
            if (name == "Invert") preview.setValue("amount", 1.0);
            const auto image = preview.grabFramebuffer();
            if (status != "Preview compiled" || image.isNull()) qFatal("%s: %s", qPrintable(name), qPrintable(status));
            if (image.pixelColor(image.width() / 4, image.height() / 2) ==
                image.pixelColor(image.width() * 3 / 4, image.height() / 2))
                qFatal("%s did not render a spatial gradient", qPrintable(name));
            if (name == "Invert") {
                preview.seek(1.0);
                if (preview.grabFramebuffer() == image) qFatal("The FX test card did not animate");
            }
        }
        const QByteArray transition = R"(/*{"INPUTS":[
            {"NAME":"startImage","TYPE":"image"},{"NAME":"endImage","TYPE":"image"},
            {"NAME":"progress","TYPE":"float","DEFAULT":0}]}*/
            void main() {
                gl_FragColor = mix(IMG_NORM_THIS_PIXEL(startImage), IMG_NORM_THIS_PIXEL(endImage), progress);
            })";
        const QByteArray red = R"(/*{}*/ void main() { gl_FragColor = vec4(1, 0, 0, 1); })";
        const QByteArray blue = R"(/*{}*/ void main() { gl_FragColor = vec4(0, 0, 1, 1); })";
        preview.load(transition, *ISFScanner::header(transition), {});
        preview.setShaderInput("startImage", red, *ISFScanner::header(red), {});
        preview.setShaderInput("endImage", blue, *ISFScanner::header(blue), {});
        preview.setTransitionAnimation(true);
        const int blueBySecond[] = {0, 0, 128, 255, 255, 128, 0};
        for (int second = 0; second <= 6; ++second) {
            preview.seek(second);
            const auto frame = preview.grabFramebuffer();
            if (frame.isNull()) qFatal("Missing transition frame");
            const auto pixel = frame.pixelColor(frame.width() / 2, frame.height() / 2);
            const int expectedBlue = blueBySecond[second];
            if (qAbs(pixel.blue() - expectedBlue) > 2 || qAbs(pixel.red() - (255 - expectedBlue)) > 2)
                qFatal("Incorrect transition frame at second %d", second);
        }
        const QByteArray multipass = R"(/*{"PASSES":[{"TARGET":"first","PERSISTENT":true},{}]}*/
            void main() {
                if (PASSINDEX == 0) gl_FragColor = vec4(0.8, 0.2, 0.4, 1.0);
                else gl_FragColor = IMG_NORM_THIS_PIXEL(first);
            })";
        preview.load(multipass, *ISFScanner::header(multipass), {});
        const auto image = preview.grabFramebuffer();
        if (status != "Preview compiled" || image.isNull()) qFatal("Multipass failed: %s", qPrintable(status));
        const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
        if (qAbs(pixel.red() - 204) > 2 || qAbs(pixel.green() - 51) > 2 || qAbs(pixel.blue() - 102) > 2)
            qFatal("Multipass preview did not sample the previous pass");
        preview.setFixedRenderSize(QSize(80, 160));
        const QByteArray resolution = R"(/*{}*/
            void main() { gl_FragColor = vec4(RENDERSIZE / 255.0, 0.25, 1.0); })";
        preview.load(resolution, *ISFScanner::header(resolution), {});
        for (const QSize widgetSize : {QSize(320, 180), QSize(420, 240)}) {
            preview.resize(widgetSize);
            const auto fixed = preview.grabFramebuffer();
            const auto center = fixed.pixelColor(fixed.width() / 2, fixed.height() / 2);
            const auto margin = fixed.pixelColor(1, fixed.height() / 2);
            if (qAbs(center.red() - 80) > 2 || qAbs(center.green() - 160) > 2 || margin == center)
                qFatal("Fixed preview did not preserve the shader resolution and aspect ratio");
        }
        QTemporaryDir fixture;
        if (!fixture.isValid()) qFatal("Could not create vertex fixture directory");
        QFile vertex(fixture.filePath("Shader.vs"));
        const QByteArray vertexCode = R"(
            varying float fromVertex;
            void main() { isf_vertShaderInit(); fromVertex = gain; })";
        if (!vertex.open(QIODevice::WriteOnly) || vertex.write(vertexCode) != vertexCode.size())
            qFatal("Could not write vertex fixture");
        vertex.close();
        const QByteArray custom = R"glsl(/*{
            "INPUTS":[{"NAME":"gain","TYPE":"float","DEFAULT":0.75}],
            "PASSES":[{"TARGET":"small","WIDTH":"floor($WIDTH * $gain)","HEIGHT":8,"FLOAT":true},{}]
        }*/
            varying float fromVertex;
            void main() {
                if (PASSINDEX == 0)
                    gl_FragColor = vec4(RENDERSIZE.x / 255.0, 2.0, fromVertex, 1.0);
                else {
                    vec4 sampleValue = IMG_NORM_THIS_PIXEL(small);
                    gl_FragColor = vec4(sampleValue.r, sampleValue.g * 0.25, sampleValue.b, 1.0);
                }
            })glsl";
        preview.load(custom, *ISFScanner::header(custom), fixture.filePath("Shader.fs"));
        const auto customImage = preview.grabFramebuffer();
        if (status != "Preview compiled" || customImage.isNull()) qFatal("Custom shader: %s", qPrintable(status));
        const auto customPixel = customImage.pixelColor(customImage.width() / 2, customImage.height() / 2);
        if (qAbs(customPixel.red() - 60) > 2 || qAbs(customPixel.green() - 128) > 2 ||
            qAbs(customPixel.blue() - 191) > 2)
            qFatal("Companion vertex, default input, or sized float pass produced the wrong pixel");

        const QByteArray oversized = R"(/*{"PASSES":[{"TARGET":"large","FLOAT":true},{}]}*/
            void main() { gl_FragColor = vec4(1.0); })";
        preview.setFixedRenderSize(QSize(4096, 4096));
        preview.load(oversized, *ISFScanner::header(oversized), {});
        preview.grabFramebuffer();
        if (!status.contains("256 MiB") && !status.contains("texture limit"))
            qFatal("Oversized buffers were not rejected: %s", qPrintable(status));
        preview.setFixedRenderSize(QSize(80, 160));
        preview.grabFramebuffer();
        if (status != "Preview compiled") qFatal("Preview did not recover after reducing the size");
        app.quit();
    });
    return app.exec();
}
