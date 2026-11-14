#include "FindBar.h"
#include "UiHelpers.h"
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QTextDocument>

FindBar::FindBar(std::function<QPlainTextEdit *()> currentEditor, QWidget *scope, QWidget *parent)
    : QWidget(parent), currentEditor(std::move(currentEditor)) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    field = new QLineEdit;
    field->setPlaceholderText("Find in source…");
    auto *previous = new QPushButton("Previous");
    auto *next = new QPushButton("Next");
    auto *closeButton = new QPushButton("Close");
    status = plainLabel();
    layout->addWidget(field, 1);
    layout->addWidget(status);
    layout->addWidget(previous);
    layout->addWidget(next);
    layout->addWidget(closeButton);
    hide();

    const auto shortcut = [scope](const QKeySequence &key, auto callback) {
        auto *action = new QShortcut(key, scope);
        action->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(action, &QShortcut::activated, scope, callback);
    };
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_F), [this] { open(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_G), [this] { find(false, false); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] { find(true, false); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &FindBar::close);
    connect(closeButton, &QPushButton::clicked, this, &FindBar::close);
    connect(previous, &QPushButton::clicked, this, [this] { find(true, false); });
    connect(next, &QPushButton::clicked, this, [this] { find(false, false); });
    connect(field, &QLineEdit::returnPressed, this, [this] { find(false, false); });
    connect(field, &QLineEdit::textChanged, this, [this] { find(false, true); });
}

void FindBar::editorChanged() {
    status->clear();
    if (!currentEditor()) hide();
    else if (isVisible()) find(false, true);
}

void FindBar::open() {
    auto *source = currentEditor();
    if (!source) return;
    const auto selection = source->textCursor().selectedText();
    show();
    if (!selection.isEmpty() && !selection.contains(QChar::ParagraphSeparator)) field->setText(selection);
    field->setFocus();
    field->selectAll();
}

void FindBar::close() {
    hide();
    if (auto *source = currentEditor()) source->setFocus();
}

void FindBar::find(bool backward, bool includeCurrent) {
    auto *source = currentEditor();
    const auto query = field->text();
    status->clear();
    if (!source || query.isEmpty()) return;
    const auto flags = backward ? QTextDocument::FindFlags(QTextDocument::FindBackward) : QTextDocument::FindFlags{};
    auto cursor = source->textCursor();
    cursor.setPosition(backward || includeCurrent ? cursor.selectionStart() : cursor.selectionEnd());
    auto match = source->document()->find(query, cursor, flags);
    if (match.isNull()) {
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        match = source->document()->find(query, cursor, flags);
    }
    if (match.isNull()) {
        status->setText("No matches");
        return;
    }
    source->setTextCursor(match);
    source->ensureCursorVisible();
}
