#include "DiagnosticsPanel.h"
#include "ISFScanner.h"
#include "ShaderDocument.h"
#include "ShaderEngine.h"
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
// Header error positions are UTF-8 byte offsets; editor positions count UTF-16 code units.
int textPosition(const QByteArray &bytes, qsizetype byteOffset) {
    return int(QString::fromUtf8(bytes.left(byteOffset)).size());
}
}

DiagnosticsPanel::DiagnosticsPanel(ShaderDocument *document, QWidget *parent)
    : QWidget(parent), document(document) {
    auto *layout = new QVBoxLayout(this);
    summary = new QLabel;
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    compilerLog = new QPlainTextEdit;
    compilerLog->setReadOnly(true);
    compilerLog->setFont(document->fragmentEditor()->font());
    compilerLog->setVisible(false);
    jump = new QPushButton("Go to error");
    fixCommas = new QPushButton("Remove trailing commas");
    fixGlsl = new QPushButton("Apply suggested fix");
    fixGlsl->hide();
    layout->addWidget(summary);
    layout->addWidget(compilerLog, 1);
    layout->addWidget(jump, 0, Qt::AlignLeft);
    layout->addWidget(fixCommas, 0, Qt::AlignLeft);
    layout->addWidget(fixGlsl, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(document, &ShaderDocument::changed, this, &DiagnosticsPanel::update);
    connect(jump, &QPushButton::clicked, this, &DiagnosticsPanel::jumpToError);
    connect(fixGlsl, &QPushButton::clicked, this, &DiagnosticsPanel::applySuggestedFix);
    connect(fixCommas, &QPushButton::clicked, this, &DiagnosticsPanel::removeTrailingCommas);
    update();
}

void DiagnosticsPanel::setCompilerDiagnostic(const QString &stage, const QString &file, const QString &log, int line) {
    compiler = {stage, file, log, document->path(), line};
    update();
}

void DiagnosticsPanel::setPreviewStatus(const QString &status, bool compiled) {
    if (compiled) {
        if (compiler.stage != DiagnosticStage::MaterialWarning) compiler = {};
    } else if (compiler.stage.isEmpty() || compiler.stage == DiagnosticStage::PreviewError ||
               compiler.stage == DiagnosticStage::MaterialWarning) {
        compiler = {DiagnosticStage::PreviewError, document->path(), status, document->path(), 0};
    }
    update();
}

// A misspelled ISF built-in, named by the compiler log and found exactly once on the reported line.
std::optional<DiagnosticsPanel::GlslFix> DiagnosticsPanel::suggestedFix() const {
    if (compiler.stage != DiagnosticStage::Fragment || compiler.file != document->path() || compiler.line <= 0 ||
        !document->isPreviewCurrent())
        return {};
    const auto log = compiler.log.toLower();
    if (!log.contains("undeclared") && !log.contains("not declared") && !log.contains("unknown identifier"))
        return {};
    const auto block = document->fragmentEditor()->document()->findBlockByNumber(compiler.line - 1);
    if (!block.isValid()) return {};
    const auto line = block.text().section("//", 0, 0);
    static const std::pair<const char *, const char *> misspellings[] = {
        {"gl_FragColour", "gl_FragColor"},
        {"isf_FragNormCoords", "isf_FragNormCoord"},
        {"vv_FragNormCoords", "vv_FragNormCoord"},
    };
    for (const auto &[wrong, right] : misspellings) {
        const QString from = QString::fromLatin1(wrong);
        const QRegularExpression word("\\b" + from + "\\b");
        if (!word.match(compiler.log).hasMatch()) continue;
        auto occurrences = word.globalMatch(line);
        if (!occurrences.hasNext()) continue;
        const auto match = occurrences.next();
        if (occurrences.hasNext()) continue;
        return GlslFix{block.position() + int(match.capturedStart()), int(match.capturedLength()),
                       from, QString::fromLatin1(right)};
    }
    return {};
}

void DiagnosticsPanel::update() {
    const bool open = document->isOpen();
    const auto bytes = document->bytes();
    ISFScanner::HeaderError error;
    const bool material = open && document->isMaterial();
    const bool invalid = open && !ISFScanner::header(bytes, &error) && !(material && document->previewHeader());
    const auto fixes = invalid ? ISFScanner::trailingCommaFix(bytes) : QList<qsizetype>{};
    const bool showCompiler = !invalid && open && compiler.shader == document->path() && !compiler.stage.isEmpty();
    const bool warning = showCompiler && compiler.stage == DiagnosticStage::MaterialWarning;
    emit summaryChanged(invalid || showCompiler ? "Diagnostics (1)" : "Diagnostics",
                        warning ? QColor("#e6bd8d") : invalid || showCompiler ? QColor("#f17979") : QColor{});
    const bool hasError = (invalid && (material || ISFScanner::headerStart(bytes) >= 0)) || (showCompiler && !warning);
    if (hasError) emit attentionRequested();
    compilerLog->setVisible(showCompiler);
    const auto glslFix = showCompiler ? suggestedFix() : std::optional<GlslFix>{};
    fixGlsl->setVisible(glslFix.has_value());
    jump->setEnabled(invalid || (showCompiler && compiler.line > 0 && document->isPreviewCurrent() &&
                                 !(compiler.file == document->vertexPath() && document->isVertexModified())));
    fixCommas->setEnabled(!fixes.isEmpty());
    if (showCompiler) {
        QString heading = compiler.stage;
        if (!compiler.file.isEmpty()) heading += " · " + QFileInfo(compiler.file).fileName();
        if (compiler.line > 0) heading += QString(" · line %1").arg(compiler.line);
        if (!document->isPreviewCurrent()) heading += "\nSave the current edits to refresh this diagnostic.";
        const auto log = compiler.log.toLower();
        if (log.contains("undeclared") || log.contains("not declared"))
            heading += "\nCheck the identifier spelling, its declaration, and its scope.";
        else if (log.contains("no matching") || log.contains("cannot convert") || log.contains("type mismatch"))
            heading += "\nCheck the types of the arguments and the expected GLSL signature.";
        else if (log.contains("syntax error"))
            heading += "\nCheck for missing punctuation or unmatched brackets near this line.";
        if (glslFix) heading += "\nSuggested fix: replace " + glslFix->from + " with " + glslFix->to + ".";
        summary->setText(heading);
        compilerLog->setPlainText(compiler.log.isEmpty() ? "The driver did not provide an error log." : compiler.log);
        return;
    }
    if (!invalid) {
        summary->setText(!open ? "Select a shader to see its diagnostics." :
                         material ? "MadMapper Material · 2D preview" : "No ISF header errors.");
        return;
    }
    const auto position = fixes.isEmpty() ? error.byteOffset : fixes.first();
    const auto prefix = QString::fromUtf8(bytes.left(position));
    const auto line = prefix.count('\n') + 1;
    const auto column = prefix.size() - prefix.lastIndexOf('\n');
    const auto detail = fixes.isEmpty() ? error.message :
        QString("%1 trailing comma(s) can be removed to repair the header.").arg(fixes.size());
    summary->setText(QString("Line %1, column %2\n%3").arg(line).arg(column).arg(detail));
}

void DiagnosticsPanel::jumpToError() {
    const auto bytes = document->bytes();
    ISFScanner::HeaderError error;
    if (ISFScanner::header(bytes, &error) || (document->isMaterial() && document->previewHeader())) {
        if (compiler.shader != document->path() || compiler.line <= 0 || !document->isPreviewCurrent()) return;
        emit compilerErrorRequested(compiler.file.isEmpty() ? document->path() : compiler.file, compiler.line);
        return;
    }
    const auto fixes = ISFScanner::trailingCommaFix(bytes);
    emit headerErrorRequested(textPosition(bytes, fixes.isEmpty() ? error.byteOffset : fixes.first()));
}

void DiagnosticsPanel::applySuggestedFix() {
    const auto fix = suggestedFix();
    if (!fix) return;
    QTextCursor cursor(document->fragmentEditor()->document());
    cursor.setPosition(fix->position);
    cursor.setPosition(fix->position + fix->length, QTextCursor::KeepAnchor);
    cursor.insertText(fix->to);
    emit message("Suggested fix applied in the editor. Save to update the file.");
}

void DiagnosticsPanel::removeTrailingCommas() {
    const auto bytes = document->bytes();
    const auto fixes = ISFScanner::trailingCommaFix(bytes);
    if (fixes.isEmpty()) return;
    QTextCursor cursor(document->fragmentEditor()->document());
    cursor.beginEditBlock();
    for (auto it = fixes.crbegin(); it != fixes.crend(); ++it) {
        cursor.setPosition(textPosition(bytes, *it));
        cursor.deleteChar();
    }
    cursor.endEditBlock();
    emit message("Trailing commas removed from the editor. Save to update the file.");
}
