#pragma once
#include <QJsonObject>
#include <QWidget>
#include <functional>

class QPushButton;
class QVBoxLayout;
class ShaderPreview;

// Controls for the open shader's ISF inputs (sliders, checkboxes, pads, color pickers, image and live
// sources). Each control drives the preview directly; the panel is rebuilt whenever the header changes.
class ParameterPanel final : public QWidget {
    Q_OBJECT
public:
    ParameterPanel(ShaderPreview *preview, QPushButton *transitionAnimation, QWidget *parent = nullptr);
    void rebuild(const QJsonObject &header, const QString &shaderPath);
    // Folder that image and shader file dialogs start in.
    std::function<QString()> browseFolder;

signals:
    void saveDefaultsRequested();

private:
    ShaderPreview *preview;
    QPushButton *transitionAnimationButton;
    QVBoxLayout *parameters;
    QString shaderPath;
};
