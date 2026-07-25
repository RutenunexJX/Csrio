#pragma once

#include "regmap/core/model.hpp"

#include <optional>
#include <string_view>

namespace regmap {

[[nodiscard]] std::optional<AccessMode> parseAccessMode(std::string_view text) noexcept;
[[nodiscard]] std::optional<FieldType> parseFieldType(std::string_view text) noexcept;
[[nodiscard]] std::optional<ReadSideEffect> parseReadSideEffect(std::string_view text) noexcept;
[[nodiscard]] std::optional<WriteSideEffect> parseWriteSideEffect(std::string_view text) noexcept;

[[nodiscard]] std::string_view toString(AccessMode value) noexcept;
[[nodiscard]] std::string_view toString(FieldType value) noexcept;
[[nodiscard]] std::string_view toString(ReadSideEffect value) noexcept;
[[nodiscard]] std::string_view toString(WriteSideEffect value) noexcept;

} // namespace regmap
