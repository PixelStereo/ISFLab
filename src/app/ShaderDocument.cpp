#include "ShaderDocument.h"
#include "ISFScanner.h"
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QTextCursor>

ShaderDocument::ShaderDocument(QPlainTextEdit *fragment, QPlainTextEdit *vertex, QObject *parent)
    : QObject(parent), fragment(fragment), vertex(vertex) {
    connect(fragment->document(), &QTextDocument::contentsChanged, this, [this] {
        invalidate();
        emit changed();
    });
    connect(vertex->document(), &QTextDocument::modificationChanged, this, &ShaderDocument::changed);
}

void ShaderDocument::setPaths(const QString &fragmentPath, const QString &vertexPath) {
    filePath = fragmentPath;
    vertexFile = vertexPath;
    invalidate();
}

void ShaderDocument::invalidate() {
    cachedBytes.reset();
    cachedHeader.reset();
    cachedPreviewHeader.reset();
    cachedMaterial.reset();
}

QString ShaderDocument::text() const { return fragment->toPlainText(); }

QByteArray ShaderDocument::bytes() const {
    if (!cachedBytes) cachedBytes = text().toUtf8();
    return *cachedBytes;
}

const std::optional<QJsonObject> &ShaderDocument::header() const {
    if (!cachedHeader) cachedHeader = ISFScanner::header(bytes());
    return *cachedHeader;
}

bool ShaderDocument::isMaterial() const {
    if (!cachedMaterial) cachedMaterial = ISFScanner::isMadMapperMaterial(bytes());
    return *cachedMaterial;
}

const std::optional<QJsonObject> &ShaderDocument::previewHeader() const {
    if (!cachedPreviewHeader) cachedPreviewHeader = isMaterial() ? ISFScanner::materialHeader(bytes()) : header();
    return *cachedPreviewHeader;
}

bool ShaderDocument::isFragmentModified() const { return fragment->document()->isModified(); }
bool ShaderDocument::isVertexModified() const { return vertex->document()->isModified(); }

void ShaderDocument::writeHeader(const QJsonObject &header) {
    const auto source = bytes();
    const auto range = ISFScanner::headerRange(source);
    if (!range.isValid()) return;
    // Editor positions count UTF-16 code units, the header range counts UTF-8 bytes.
    const auto start = QString::fromUtf8(source.left(range.start)).size();
    const auto end = start + QString::fromUtf8(source.mid(range.start, range.end - range.start)).size();
    auto json = QString::fromUtf8(QJsonDocument(header).toJson(QJsonDocument::Indented));
    json.replace("*/", "*\\/");
    QTextCursor cursor(fragment->document());
    cursor.setPosition(start + 2);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(json);
}
