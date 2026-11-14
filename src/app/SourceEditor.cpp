#include "SourceEditor.h"
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>

class SourceEditor::LineNumbers final : public QWidget {
public:
    explicit LineNumbers(SourceEditor *editor) : QWidget(editor), editor(editor) {}
protected:
    void paintEvent(QPaintEvent *event) override { editor->paintGutter(event->rect()); }
private:
    SourceEditor *editor;
};

SourceEditor::SourceEditor(QWidget *parent) : QPlainTextEdit(parent), gutter(new LineNumbers(this)) {
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { layoutGutter(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy) gutter->scroll(0, dy);
        else gutter->update(0, rect.y(), gutter->width(), rect.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, gutter, [this] { gutter->update(); });
    layoutGutter();
}

void SourceEditor::goToLine(int line) {
    const auto block = document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    setTextCursor(QTextCursor(block));
    setFocus();
    ensureCursorVisible();
}

void SourceEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    layoutGutter();
}

void SourceEditor::changeEvent(QEvent *event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange) layoutGutter();
}

void SourceEditor::layoutGutter() {
    const int digits = QString::number(qMax(1, blockCount())).size();
    const int width = 14 + digits * fontMetrics().horizontalAdvance('9');
    if (viewportMargins().left() != width) setViewportMargins(width, 0, 0, 0);
    const auto rect = contentsRect();
    gutter->setGeometry(rect.left(), rect.top(), width, rect.height());
}

void SourceEditor::paintGutter(const QRect &area) {
    QPainter painter(gutter);
    painter.fillRect(area, palette().color(QPalette::Window));
    painter.setFont(font());
    const auto current = textCursor().blockNumber();
    for (auto block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        if (top > area.bottom()) break;
        if (!block.isVisible() || top + blockBoundingRect(block).height() < area.top()) continue;
        painter.setPen(palette().color(block.blockNumber() == current ? QPalette::Text : QPalette::PlaceholderText));
        painter.drawText(QRect(0, top, gutter->width() - 7, fontMetrics().height()),
                         Qt::AlignRight | Qt::AlignVCenter, QString::number(block.blockNumber() + 1));
    }
}

void SourceHighlighter::highlightBlock(const QString &line) {
    enum State { BeforeSource, GLSL, Header, Comment };
    int state = qMax(previousBlockState(), int(BeforeSource));
    int offset = 0;
    if (state == BeforeSource) {
        while (offset < line.size() && line.at(offset).isSpace()) ++offset;
        if (offset == line.size()) {
            setCurrentBlockState(BeforeSource);
            return;
        }
        if (line.mid(offset, 2) == "//") {
            setFormat(offset, line.size() - offset, QColor("#83918d"));
            setCurrentBlockState(BeforeSource);
            return;
        }
        state = GLSL;
        if (line.mid(offset, 2) == "/*") {
            setFormat(offset, 2, QColor("#83918d"));
            offset += 2;
            state = Header;
        }
    }

    static const QRegularExpression json(R"rx("(?:\\.|[^"\\])*"(?=\s*:)|"(?:\\.|[^"\\])*"|-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|\b(?:true|false|null)\b)rx");
    static const QRegularExpression glsl(
        R"rx(//.*|/\*|^\s*#\s*\w+|\b(?:attribute|varying|uniform|const|in|out|inout|precision|highp|mediump|lowp|if|else|for|while|do|switch|case|default|break|continue|return|discard|struct|true|false)\b|\b(?:void|bool|int|uint|float|double|[biud]?vec[234]|mat[234](?:x[234])?|sampler\w+)\b|\b(?:gl_\w+|isf_\w+|IMG_\w+|TIME|TIMEDELTA|DATE|RENDERSIZE|PASSINDEX|FRAMEINDEX)\b|(?:\b0[xX][\da-fA-F]+|\b\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fFuU]?|\b[A-Za-z_]\w*(?=\s*\())rx");
    while (offset < line.size()) {
        if (state == Header || state == Comment) {
            const int close = line.indexOf("*/", offset);
            const int end = close < 0 ? line.size() : close + 2;
            setFormat(offset, end - offset, QColor("#83918d"));
            if (state == Header) {
                auto tokens = json.globalMatch(line.mid(offset, (close < 0 ? end : close) - offset));
                while (tokens.hasNext()) {
                    const auto token = tokens.next();
                    const auto value = token.capturedView();
                    const auto after = line.mid(offset + token.capturedEnd()).trimmed();
                    const QColor color(value.startsWith('"')
                        ? (after.startsWith(':') ? "#86cbd0" : "#b8d895") : "#efbb83");
                    setFormat(offset + token.capturedStart(), token.capturedLength(), color);
                }
            }
            offset = end;
            if (close >= 0) state = GLSL;
            continue;
        }

        const auto token = glsl.match(line, offset);
        if (!token.hasMatch()) break;
        const auto value = token.capturedView();
        offset = token.capturedEnd();
        QColor color("#c9a8ed");
        if (value.startsWith('/')) {
            color = QColor("#83918d");
            if (value == u"/*") state = Comment;
        } else if (value.trimmed().startsWith('#')) {
            color = QColor("#dfa6ca");
        } else if (value.front().isDigit() || value.startsWith('.')) {
            color = QColor("#efbb83");
        } else if (value.startsWith(u"gl_") || value.startsWith(u"isf_") ||
                   value.front().isUpper() || line.mid(offset).trimmed().startsWith('(')) {
            color = QColor("#86cbd0");
        }
        setFormat(token.capturedStart(), token.capturedLength(), color);
    }
    setCurrentBlockState(state);
}
