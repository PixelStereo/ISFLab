#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <optional>

class QPlainTextEdit;

// The shader open in the editor: its fragment and vertex editors, file paths, and the source last sent
// to the preview. Parsing results are cached until the fragment text changes, so the panels that
// observe the header share one parse per edit. Panels connect to changed(), never to the editor.
class ShaderDocument final : public QObject {
    Q_OBJECT
public:
    ShaderDocument(QPlainTextEdit *fragment, QPlainTextEdit *vertex, QObject *parent = nullptr);

    QPlainTextEdit *fragmentEditor() const { return fragment; }
    QPlainTextEdit *vertexEditor() const { return vertex; }

    QString path() const { return filePath; }
    QString vertexPath() const { return vertexFile; }
    bool isOpen() const { return !filePath.isEmpty(); }
    void setPaths(const QString &fragmentPath, const QString &vertexPath);

    QString text() const;
    QByteArray bytes() const;
    // Strict ISF header of the editor text.
    const std::optional<QJsonObject> &header() const;
    // Header the preview uses: the lenient MadMapper parse for Materials, the strict one otherwise.
    const std::optional<QJsonObject> &previewHeader() const;
    bool isMaterial() const;
    bool isFragmentModified() const;
    bool isVertexModified() const;
    bool isModified() const { return isFragmentModified() || isVertexModified(); }

    // The text last loaded into the preview; diagnostics refer to it.
    void markPreviewed() { previewed = text(); }
    void clearPreviewed() { previewed.clear(); }
    bool isPreviewCurrent() const { return text() == previewed; }

    // Replaces the JSON header in the editor (an undoable edit), keeping the GLSL below it.
    void writeHeader(const QJsonObject &header);

signals:
    void changed();

private:
    void invalidate();

    QPlainTextEdit *fragment;
    QPlainTextEdit *vertex;
    QString filePath, vertexFile, previewed;
    mutable std::optional<QByteArray> cachedBytes;
    mutable std::optional<std::optional<QJsonObject>> cachedHeader, cachedPreviewHeader;
    mutable std::optional<bool> cachedMaterial;
};
