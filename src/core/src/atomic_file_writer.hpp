#pragma once

#include <QByteArray>
#include <QFileDevice>
#include <QIODevice>
#include <QString>

namespace regmap::detail {

struct AtomicWriteResult {
    bool committed{false};
    const char* operation{"read"};
    QFileDevice::FileError error{QFileDevice::NoError};
    QString errorText;
    int attempts{0};
};

// Captures the destination before output preparation. Changes observed before
// replacement abort the write; this is not a cross-process compare-and-swap.
class AtomicFileWriter {
public:
    explicit AtomicFileWriter(QString path);

    // Allows callers to restore attributes only while the destination matches its snapshot.
    [[nodiscard]] bool destinationUnchanged() const;

    [[nodiscard]] AtomicWriteResult write(
        const QByteArray& bytes,
        QIODevice::OpenMode mode = QIODevice::WriteOnly) const;

private:
    struct Snapshot {
        QString resolvedPath;
        bool exists{false};
        QByteArray digest;
        QFileDevice::FileError error{QFileDevice::NoError};
        QString errorText;
    };

    [[nodiscard]] static Snapshot capture(const QString& path);
    [[nodiscard]] AtomicWriteResult checkDestination(int attempts) const;

    QString path_;
    Snapshot initial_;
};

} // namespace regmap::detail
