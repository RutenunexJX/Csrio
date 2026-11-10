#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"

#include <cstdint>
#include <vector>
#include <stop_token>

namespace regmap {

struct XlsxExportResult {
    std::vector<std::uint8_t> bytes;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] XlsxExportResult exportReadOnlyWorkbook(const Workspace& workspace, bool consecutiveEnums = false,
                                                      std::stop_token cancellation = {});

} // namespace regmap
