#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

// Plain text editor with a line number gutter, used for GLSL sources.
class SourceEditor final : public QPlainTextEdit {
public:
    explicit SourceEditor(QWidget *parent = nullptr);
    void goToLine(int line);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    class LineNumbers;
    void layoutGutter();
    void paintGutter(const QRect &area);

    QWidget *gutter;
};

// Highlights the ISF JSON header and the GLSL code that follows it.
class SourceHighlighter final : public QSyntaxHighlighter {
public:
    explicit SourceHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString &line) override;
};
