#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace regmap {

class UnsignedValue {
public:
    UnsignedValue() = default;
    explicit UnsignedValue(std::uint64_t value);

    [[nodiscard]] static std::optional<UnsignedValue> parse(std::string_view text);
    [[nodiscard]] static UnsignedValue bitMask(std::size_t lsb, std::size_t width);

    [[nodiscard]] bool isZero() const noexcept;
    [[nodiscard]] std::size_t bitWidth() const noexcept;
    [[nodiscard]] bool fitsInBits(std::size_t width) const noexcept;
    [[nodiscard]] bool testBit(std::size_t index) const noexcept;
    [[nodiscard]] UnsignedValue slice(std::size_t lsb, std::size_t width) const;

    [[nodiscard]] std::string toHexString(bool includePrefix = true) const;
    [[nodiscard]] std::string toDecimalString() const;
    [[nodiscard]] std::optional<std::uint64_t> toUInt64() const noexcept;

    friend bool operator==(const UnsignedValue&, const UnsignedValue&) = default;

private:
    static constexpr std::size_t bitsPerWord = 32;

    std::vector<std::uint32_t> words_;

    void normalize() noexcept;
    void multiplySmall(std::uint32_t multiplier);
    void addSmall(std::uint32_t addend);
    [[nodiscard]] std::uint32_t divideSmall(std::uint32_t divisor);
    void setBit(std::size_t index);
};

} // namespace regmap
