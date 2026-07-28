#include "regmap/core/unsigned_value.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace regmap {
namespace {

[[nodiscard]] int digitValue(char character) noexcept
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

UnsignedValue::UnsignedValue(std::uint64_t value)
{
    if (value == 0) {
        return;
    }

    words_.push_back(static_cast<std::uint32_t>(value));
    const auto upper = static_cast<std::uint32_t>(value >> bitsPerWord);
    if (upper != 0) {
        words_.push_back(upper);
    }
}

std::optional<UnsignedValue> UnsignedValue::parse(std::string_view text)
{
    text = trim(text);
    if (text.empty() || text.front() == '-') {
        return std::nullopt;
    }
    if (text.front() == '+') {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }

    std::uint32_t base = 10;
    if (text.size() >= 2 && text.front() == '0') {
        switch (text[1]) {
        case 'x':
        case 'X':
            base = 16;
            text.remove_prefix(2);
            break;
        case 'b':
        case 'B':
            base = 2;
            text.remove_prefix(2);
            break;
        case 'o':
        case 'O':
            base = 8;
            text.remove_prefix(2);
            break;
        default:
            break;
        }
    }

    UnsignedValue result;
    bool hasDigit = false;
    bool previousWasSeparator = false;
    for (const char character : text) {
        if (character == '_') {
            if (!hasDigit || previousWasSeparator) {
                return std::nullopt;
            }
            previousWasSeparator = true;
            continue;
        }

        const int digit = digitValue(character);
        if (digit < 0 || static_cast<std::uint32_t>(digit) >= base) {
            return std::nullopt;
        }

        result.multiplySmall(base);
        result.addSmall(static_cast<std::uint32_t>(digit));
        hasDigit = true;
        previousWasSeparator = false;
    }

    if (!hasDigit || previousWasSeparator) {
        return std::nullopt;
    }

    result.normalize();
    return result;
}

UnsignedValue UnsignedValue::bitMask(std::size_t lsb, std::size_t width)
{
    UnsignedValue result;
    for (std::size_t index = 0; index < width; ++index) {
        result.setBit(lsb + index);
    }
    return result;
}

bool UnsignedValue::isZero() const noexcept
{
    return words_.empty();
}

std::size_t UnsignedValue::bitWidth() const noexcept
{
    if (words_.empty()) {
        return 0;
    }

    const auto leadingZeroes = std::countl_zero(words_.back());
    return (words_.size() - 1) * bitsPerWord
        + (bitsPerWord - static_cast<std::size_t>(leadingZeroes));
}

bool UnsignedValue::fitsInBits(std::size_t width) const noexcept
{
    return bitWidth() <= width;
}

bool UnsignedValue::testBit(std::size_t index) const noexcept
{
    const std::size_t wordIndex = index / bitsPerWord;
    if (wordIndex >= words_.size()) {
        return false;
    }
    const std::size_t bitIndex = index % bitsPerWord;
    return (words_[wordIndex] & (std::uint32_t {1} << bitIndex)) != 0;
}

UnsignedValue UnsignedValue::slice(std::size_t lsb, std::size_t width) const
{
    UnsignedValue result;
    for (std::size_t index = 0; index < width; ++index) {
        if (testBit(lsb + index)) {
            result.setBit(index);
        }
    }
    return result;
}

std::optional<UnsignedValue>
UnsignedValue::replacingSlice(std::size_t lsb, std::size_t width,
                              const UnsignedValue& replacement) const
{
    if (!replacement.fitsInBits(width) ||
        width > std::numeric_limits<std::size_t>::max() - lsb) {
        return std::nullopt;
    }

    UnsignedValue result = *this;
    if (width == 0) {
        return result;
    }
    const std::size_t end = lsb + width;
    const std::size_t requiredWords = (end - 1) / bitsPerWord + 1;
    if (result.words_.size() < requiredWords) {
        result.words_.resize(requiredWords, 0);
    }
    for (std::size_t index = 0; index < width; ++index) {
        const std::size_t target = lsb + index;
        const std::size_t wordIndex = target / bitsPerWord;
        const std::size_t bitIndex = target % bitsPerWord;
        const std::uint32_t mask = std::uint32_t{1} << bitIndex;
        if (replacement.testBit(index)) {
            result.words_[wordIndex] |= mask;
        } else {
            result.words_[wordIndex] &= ~mask;
        }
    }
    result.normalize();
    return result;
}

std::string UnsignedValue::toHexString(bool includePrefix) const
{
    if (words_.empty()) {
        return includePrefix ? "0x0" : "0";
    }

    std::ostringstream output;
    if (includePrefix) {
        output << "0x";
    }
    output << std::hex << std::uppercase << words_.back();
    for (auto iterator = words_.rbegin() + 1; iterator != words_.rend(); ++iterator) {
        output << std::setw(8) << std::setfill('0') << *iterator;
    }
    return output.str();
}

std::string UnsignedValue::toDecimalString() const
{
    if (words_.empty()) {
        return "0";
    }

    UnsignedValue quotient = *this;
    std::string digits;
    while (!quotient.isZero()) {
        const auto remainder = quotient.divideSmall(10);
        digits.push_back(static_cast<char>('0' + remainder));
    }
    std::reverse(digits.begin(), digits.end());
    return digits;
}

std::optional<std::uint64_t> UnsignedValue::toUInt64() const noexcept
{
    if (words_.size() > 2) {
        return std::nullopt;
    }
    if (words_.empty()) {
        return std::uint64_t {0};
    }

    std::uint64_t result = words_.front();
    if (words_.size() == 2) {
        result |= static_cast<std::uint64_t>(words_[1]) << bitsPerWord;
    }
    return result;
}

void UnsignedValue::normalize() noexcept
{
    while (!words_.empty() && words_.back() == 0) {
        words_.pop_back();
    }
}

void UnsignedValue::multiplySmall(std::uint32_t multiplier)
{
    if (multiplier == 0 || words_.empty()) {
        if (multiplier == 0) {
            words_.clear();
        }
        return;
    }

    std::uint64_t carry = 0;
    for (auto& word : words_) {
        const std::uint64_t product = static_cast<std::uint64_t>(word) * multiplier + carry;
        word = static_cast<std::uint32_t>(product);
        carry = product >> bitsPerWord;
    }
    if (carry != 0) {
        words_.push_back(static_cast<std::uint32_t>(carry));
    }
}

void UnsignedValue::addSmall(std::uint32_t addend)
{
    std::uint64_t carry = addend;
    std::size_t index = 0;
    while (carry != 0) {
        if (index == words_.size()) {
            words_.push_back(0);
        }
        const std::uint64_t sum = static_cast<std::uint64_t>(words_[index]) + carry;
        words_[index] = static_cast<std::uint32_t>(sum);
        carry = sum >> bitsPerWord;
        ++index;
    }
}

std::uint32_t UnsignedValue::divideSmall(std::uint32_t divisor)
{
    std::uint64_t remainder = 0;
    for (auto iterator = words_.rbegin(); iterator != words_.rend(); ++iterator) {
        const std::uint64_t dividend = (remainder << bitsPerWord) | *iterator;
        *iterator = static_cast<std::uint32_t>(dividend / divisor);
        remainder = dividend % divisor;
    }
    normalize();
    return static_cast<std::uint32_t>(remainder);
}

void UnsignedValue::setBit(std::size_t index)
{
    const std::size_t wordIndex = index / bitsPerWord;
    if (words_.size() <= wordIndex) {
        words_.resize(wordIndex + 1, 0);
    }
    const std::size_t bitIndex = index % bitsPerWord;
    words_[wordIndex] |= std::uint32_t {1} << bitIndex;
}

} // namespace regmap
