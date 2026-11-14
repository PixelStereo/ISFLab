#pragma once
#include <QColor>
#include <QWidget>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class ShaderDocument;

// Shows what prevents the open shader from previewing: an invalid JSON header (with a trailing comma
// repair when one validates) or the GLSL compiler log (with a suggested fix for common misspellings).
class DiagnosticsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(ShaderDocument *document, QWidget *parent = nullptr);

    void setCompilerDiagnostic(const QString &stage, const QString &file, const QString &log, int line);
    void setPreviewStatus(const QString &status, bool compiled);

signals:
    // Tab caption and color that summarize the current state ("Diagnostics (1)" in red, for example).
    void summaryChanged(const QString &caption, const QColor &color);
    // An error the user should see: the host shows this panel.
    void attentionRequested();
    // Go to a compiler error: file is the fragment, its vertex companion or a Material include.
    void compilerErrorRequested(const QString &file, int line);
    // Go to a header error, as a character position in the fragment editor.
    void headerErrorRequested(int position);
    void message(const QString &text);

private:
    struct CompilerDiagnostic {
        QString stage, file, log;
        QString shader; // the shader that was open when the diagnostic arrived
        int line = 0;
    };
    struct GlslFix {
        int position;
        int length;
        QString from, to;
    };
    std::optional<GlslFix> suggestedFix() const;
    void update();
    void jumpToError();
    void applySuggestedFix();
    void removeTrailingCommas();

    ShaderDocument *document;
    CompilerDiagnostic compiler;
    QLabel *summary;
    QPlainTextEdit *compilerLog;
    QPushButton *jump, *fixCommas, *fixGlsl;
};
