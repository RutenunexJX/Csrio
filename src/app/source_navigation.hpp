#pragma once

#include "regmap/core/model.hpp"

#include <filesystem>

namespace SourceNavigation {

[[nodiscard]] bool open(const regmap::SourceLocation& source);
[[nodiscard]] bool openFile(const std::filesystem::path& path);

} // namespace SourceNavigation
