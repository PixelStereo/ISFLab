#include "ZipExport.h"
#include <QDataStream>
#include <QDirIterator>
#include <QSaveFile>
#include <array>
#include <limits>

namespace {
quint32 crc32(const QByteArray &data) {
    static const auto table = [] {
        std::array<quint32, 256> values{};
        for (quint32 i = 0; i < values.size(); ++i) {
            quint32 crc = i;
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
            values[i] = crc;
        }
        return values;
    }();
    quint32 crc = 0xffffffffu;
    for (unsigned char byte : data) crc = (crc >> 8) ^ table[(crc ^ byte) & 0xff];
    return crc ^ 0xffffffffu;
}

struct Record {
    QByteArray name;
    quint32 offset, size, checksum;
    bool directory;
};
}

QString writeLibraryZip(const QString &folder, const QString &archive) {
    QSaveFile file(archive);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    constexpr auto maxSize = std::numeric_limits<quint32>::max();
    constexpr auto maxCount = std::numeric_limits<quint16>::max();
    constexpr quint16 utf8 = 0x0800, version = 20, jan1980 = 0x21;
    QList<Record> records;
    const QDir root(folder);
    QDirIterator it(folder, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const bool directory = it.fileInfo().isDir();
        const auto name = (root.relativeFilePath(it.filePath()) + (directory ? "/" : "")).toUtf8();
        QByteArray bytes;
        if (!directory) {
            QFile input(it.filePath());
            if (!input.open(QIODevice::ReadOnly)) return input.errorString();
            if (input.size() >= maxSize) return "An export file is too large for ZIP.";
            bytes = input.readAll();
            if (input.error() != QFile::NoError) return input.errorString();
        }
        if (name.size() > maxCount || records.size() >= maxCount - 1 ||
            file.pos() + 30 + name.size() + bytes.size() >= maxSize)
            return "This export is too large for ZIP. Export to a folder instead.";
        const Record record{name, quint32(file.pos()), quint32(bytes.size()), crc32(bytes), directory};
        records << record;
        out << quint32(0x04034b50) << version << utf8 << quint16(0) << quint16(0) << jan1980
            << record.checksum << record.size << record.size << quint16(name.size()) << quint16(0);
        out.writeRawData(name.constData(), name.size());
        if (!bytes.isEmpty()) out.writeRawData(bytes.constData(), bytes.size());
        if (out.status() != QDataStream::Ok) return "Cannot write ZIP archive.";
    }
    const auto directoryOffset = file.pos();
    for (const auto &record : records) {
        if (file.pos() + 46 + record.name.size() + 22 >= maxSize) return "ZIP archive is too large.";
        out << quint32(0x02014b50) << version << version << utf8 << quint16(0) << quint16(0) << jan1980
            << record.checksum << record.size << record.size << quint16(record.name.size()) << quint16(0)
            << quint16(0) << quint16(0) << quint16(0) << quint32(record.directory ? 0x10 : 0) << record.offset;
        out.writeRawData(record.name.constData(), record.name.size());
    }
    const auto directorySize = file.pos() - directoryOffset;
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(records.size())
        << quint16(records.size()) << quint32(directorySize) << quint32(directoryOffset) << quint16(0);
    if (out.status() != QDataStream::Ok) return "Cannot write ZIP archive.";
    return file.commit() ? QString() : file.errorString();
}
