#pragma once
#include <QTabWidget>

class ShaderDocument;

// Editable view of the shader's JSON header: metadata fields, the INPUTS table and the PASSES table.
// Every edit rewrites the header in the editor through the ShaderDocument (so it can be undone).
class HeaderInspector final : public QTabWidget {
    Q_OBJECT
public:
    explicit HeaderInspector(ShaderDocument *document, QWidget *parent = nullptr);

signals:
    void message(const QString &text);

private:
    QWidget *createMetadata();
    void addTable(const QString &key, const QStringList &columns, QWidget *basicInfo = nullptr);

    ShaderDocument *document;
};
