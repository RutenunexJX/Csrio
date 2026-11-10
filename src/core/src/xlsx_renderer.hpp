#pragma once

#include "regmap/core/xlsx_export.hpp"

namespace regmap::detail {
// Only the isolated renderer may enter the Qt font/layout implementation.
XlsxExportResult renderReadOnlyWorkbook(const Workspace& workspace, bool consecutiveEnums);
}
