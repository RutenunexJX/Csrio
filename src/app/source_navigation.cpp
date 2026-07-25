#include "source_navigation.hpp"

#include <QDesktopServices>
#include <QString>
#include <QUrl>

namespace {

[[nodiscard]] QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

} // namespace

namespace SourceNavigation {

bool openFile(const std::filesystem::path& path)
{
    if (path.empty()) {
        return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fromPath(path)));
}

bool open(const regmap::SourceLocation& source)
{
    if (source.workbook.empty()) {
        return false;
    }

    return openFile(source.workbook);
}

} // namespace SourceNavigation
