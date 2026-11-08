#pragma once
#include <QString>

// Writes a UTF-8 ZIP archive without compression or external tools.
QString writeLibraryZip(const QString &folder, const QString &archive);
