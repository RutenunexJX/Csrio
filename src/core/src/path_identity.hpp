#pragma once

#include <QtCore/qglobal.h>

#include <filesystem>

#ifdef Q_OS_WIN
#include <QString>
#endif

namespace regmap::detail {

#ifdef Q_OS_WIN
using PathIdentity = QString;
#else
using PathIdentity = std::filesystem::path;
#endif

[[nodiscard]] inline PathIdentity pathIdentity(const std::filesystem::path& path)
{
    std::filesystem::path normalized = path.lexically_normal();
#ifdef Q_OS_WIN
    normalized.make_preferred();
    return QString::fromStdWString(normalized.native()).toCaseFolded();
#else
    return normalized;
#endif
}

[[nodiscard]] inline bool samePathIdentity(const std::filesystem::path& left,
                                           const std::filesystem::path& right)
{
    return pathIdentity(left) == pathIdentity(right);
}

} // namespace regmap::detail
