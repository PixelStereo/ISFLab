#pragma once
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Find bar for the source editors. Ctrl+F opens it, Ctrl+G and Ctrl+Shift+G go to the next and previous
// match, Escape closes it. Searches wrap around the end of the document.
class FindBar final : public QWidget {
    Q_OBJECT
public:
    // currentEditor returns the editor to search, or nullptr when the visible tab is not a source.
    // Shortcuts are active while focus is inside scope.
    FindBar(std::function<QPlainTextEdit *()> currentEditor, QWidget *scope, QWidget *parent = nullptr);
    // Call when the visible editor changes.
    void editorChanged();

private:
    void open();
    void close();
    void find(bool backward, bool includeCurrent);

    std::function<QPlainTextEdit *()> currentEditor;
    QLineEdit *field;
    QLabel *status;
};
