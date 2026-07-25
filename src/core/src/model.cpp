#include "regmap/core/model.hpp"

namespace regmap {

bool SourceLocation::empty() const noexcept
{
    return workbook.empty() && sheet.empty() && !row.has_value() && !column.has_value()
        && cell.empty();
}

std::uint64_t Field::width() const noexcept
{
    if (msb < lsb) {
        return 0;
    }
    return static_cast<std::uint64_t>(msb) - static_cast<std::uint64_t>(lsb) + 1;
}

} // namespace regmap
