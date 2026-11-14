#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class ShaderPreview;

// The rendered preview with its transport (play, reset, timeline), render resolution, audio source and
// transition animation controls, and a one-line status.
class PreviewPanel final : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ShaderPreview *shaderPreview() const { return preview; }
    QPushButton *transitionAnimationButton() const { return transitionButton; }
    void setStatusText(const QString &text);

private:
    ShaderPreview *preview;
    QPushButton *transitionButton;
    QLabel *status;
};
