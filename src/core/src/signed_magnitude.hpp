#pragma once

#include "regmap/core/unsigned_value.hpp"

#include <cctype>
#include <optional>
#include <string_view>

namespace regmap::detail {

struct SignedMagnitude {
    bool negative{false};
    UnsignedValue magnitude;
};

[[nodiscard]] inline std::optional<SignedMagnitude> parseSignedMagnitude(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    bool negative = false;
    if (text.front() == '-' || text.front() == '+') {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto magnitude = UnsignedValue::parse(text);
    if (!magnitude) {
        return std::nullopt;
    }
    return SignedMagnitude{negative && !magnitude->isZero(), *magnitude};
}

} // namespace regmap::detail
