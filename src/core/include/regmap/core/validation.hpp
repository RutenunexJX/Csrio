#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"

#include <vector>

namespace regmap {

[[nodiscard]] std::vector<Diagnostic> validateWorkspace(const Workspace& workspace);

} // namespace regmap
