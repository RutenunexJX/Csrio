#include "regmap/core/validation.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view missingIdentityCode = "RM3000";
constexpr std::string_view duplicateIdCode = "RM3001";
constexpr std::string_view duplicateNameCode = "RM3002";
constexpr std::string_view addressWidthCode = "RM3010";
constexpr std::string_view addressRangeCode = "RM3011";
constexpr std::string_view registerWidthCode = "RM3020";
constexpr std::string_view arrayCountCode = "RM3021";
constexpr std::string_view strideCode = "RM3022";
constexpr std::string_view registerRangeCode = "RM3023";
constexpr std::string_view addressOverlapCode = "RM3024";
constexpr std::string_view blockOverlapCode = "RM3025";
constexpr std::string_view fieldRangeCode = "RM3030";
constexpr std::string_view fieldOverlapCode = "RM3031";
constexpr std::string_view accessConflictCode = "RM3032";
constexpr std::string_view sideEffectCode = "RM3033";
constexpr std::string_view resetWidthCode = "RM3034";
constexpr std::string_view resetMismatchCode = "RM3035";
constexpr std::string_view fieldTypeCode = "RM3036";
constexpr std::string_view enumWidthCode = "RM3040";
constexpr std::string_view duplicateEnumCode = "RM3041";
constexpr std::string_view enumTypeCode = "RM3042";
constexpr std::string_view enumResetCode = "RM3043";
constexpr std::string_view reservedRegisterCode = "RM3050";
constexpr std::string_view tagCode = "RM3051";
constexpr std::string_view numericRangeCode = "RM3052";
constexpr std::string_view compoundFieldCode = "RM3053";
constexpr std::string_view registerInitialCode = "RM3054";

struct AddressInterval {
    std::uint64_t first{0};
    std::uint64_t last{0};
    const Register* reg{nullptr};
    std::uint32_t instance{0};
};

struct BlockAddressInterval {
    std::uint64_t first{0};
    std::uint64_t last{0};
    const RegisterBlock* block{nullptr};
};

[[nodiscard]] bool addOverflow(std::uint64_t left, std::uint64_t right,
                               std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return true;
    }
    result = left + right;
    return false;
}

[[nodiscard]] bool multiplyOverflow(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return true;
    }
    result = left * right;
    return false;
}

template <typename Object>
[[nodiscard]] SourceLocation propertySource(const Object& object, std::string_view property)
{
    const auto iterator = object.propertySources.find(std::string(property));
    return iterator == object.propertySources.end() ? object.source : iterator->second;
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, std::string_view code, std::string message,
                   const ObjectId& objectId, SourceLocation source,
                   DiagnosticSeverity severity = DiagnosticSeverity::error)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = std::move(message);
    diagnostic.objectId = objectId;
    diagnostic.source = std::move(source);
    diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] bool canRead(AccessMode access) noexcept
{
    return access == AccessMode::readOnly || access == AccessMode::readWrite;
}

[[nodiscard]] bool canWrite(AccessMode access) noexcept
{
    return access == AccessMode::writeOnly || access == AccessMode::readWrite;
}

struct SignedMagnitude {
    bool negative{false};
    UnsignedValue magnitude;
};

[[nodiscard]] std::optional<SignedMagnitude> parseSignedMagnitude(std::string_view text)
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

[[nodiscard]] int compareMagnitude(const UnsignedValue& left, const UnsignedValue& right)
{
    if (left.bitWidth() != right.bitWidth()) {
        return left.bitWidth() < right.bitWidth() ? -1 : 1;
    }
    const std::string leftText = left.toHexString(false);
    const std::string rightText = right.toHexString(false);
    if (leftText == rightText) {
        return 0;
    }
    return leftText < rightText ? -1 : 1;
}

[[nodiscard]] int compareSigned(const SignedMagnitude& left, const SignedMagnitude& right)
{
    if (left.negative != right.negative) {
        return left.negative ? -1 : 1;
    }
    const int magnitude = compareMagnitude(left.magnitude, right.magnitude);
    return left.negative ? -magnitude : magnitude;
}

[[nodiscard]] bool fitsNumericType(const SignedMagnitude& value, FieldType type,
                                   std::uint64_t width)
{
    if (width == 0) {
        return false;
    }
    if (type == FieldType::unsignedInteger) {
        return !value.negative && value.magnitude.fitsInBits(width);
    }
    if (type != FieldType::signedInteger) {
        return false;
    }
    if (!value.negative) {
        return width == 1 ? value.magnitude.isZero() : value.magnitude.fitsInBits(width - 1);
    }
    if (value.magnitude.fitsInBits(width - 1)) {
        return true;
    }
    return value.magnitude == UnsignedValue::bitMask(width - 1, 1);
}

[[nodiscard]] int compareTwosComplementMagnitude(const UnsignedValue& encoded,
                                                  std::uint64_t width,
                                                  const UnsignedValue& magnitude)
{
    std::uint64_t leastSetBit = 0;
    while (leastSetBit < width && !encoded.testBit(leastSetBit)) {
        ++leastSetBit;
    }

    const std::uint64_t comparisonWidth =
        std::max<std::uint64_t>(width, magnitude.bitWidth());
    for (std::uint64_t index = comparisonWidth; index > 0; --index) {
        const std::uint64_t bit = index - 1;
        bool encodedMagnitudeBit = false;
        if (bit < width) {
            if (bit == leastSetBit) {
                encodedMagnitudeBit = true;
            } else if (bit > leastSetBit) {
                encodedMagnitudeBit = !encoded.testBit(bit);
            }
        }
        const bool boundMagnitudeBit = magnitude.testBit(bit);
        if (encodedMagnitudeBit != boundMagnitudeBit) {
            return encodedMagnitudeBit ? 1 : -1;
        }
    }
    return 0;
}

[[nodiscard]] int compareEncodedNumericValue(const UnsignedValue& encoded, FieldType type,
                                             std::uint64_t width,
                                             const SignedMagnitude& bound)
{
    const bool negative =
        type == FieldType::signedInteger && width > 0 && encoded.testBit(width - 1);
    if (negative != bound.negative) {
        return negative ? -1 : 1;
    }
    if (!negative) {
        return compareMagnitude(encoded, bound.magnitude);
    }
    return -compareTwosComplementMagnitude(encoded, width, bound.magnitude);
}

[[nodiscard]] bool encodedNumericValueIsInRange(
    const UnsignedValue& encoded, FieldType type, std::uint64_t width,
    const std::optional<SignedMagnitude>& minimum,
    const std::optional<SignedMagnitude>& maximum)
{
    return (!minimum || compareEncodedNumericValue(encoded, type, width, *minimum) >= 0) &&
           (!maximum || compareEncodedNumericValue(encoded, type, width, *maximum) <= 0);
}

[[nodiscard]] bool diagnosticLess(const Diagnostic& left, const Diagnostic& right)
{
    const auto severityRank = [](DiagnosticSeverity severity) {
        switch (severity) {
        case DiagnosticSeverity::error:
            return 0;
        case DiagnosticSeverity::warning:
            return 1;
        case DiagnosticSeverity::information:
            return 2;
        }
        return 3;
    };

    return std::tuple{severityRank(left.severity),
                      left.source.workbook.generic_string(),
                      left.source.sheet,
                      left.source.row.value_or(0),
                      left.source.column.value_or(0),
                      left.code,
                      left.objectId,
                      left.message} < std::tuple{severityRank(right.severity),
                                                 right.source.workbook.generic_string(),
                                                 right.source.sheet,
                                                 right.source.row.value_or(0),
                                                 right.source.column.value_or(0),
                                                 right.code,
                                                 right.objectId,
                                                 right.message};
}

class Validator {
public:
    explicit Validator(const Workspace& workspace)
        : workspace_(workspace)
    {
    }

    [[nodiscard]] std::vector<Diagnostic> run()
    {
        validateWorkspaceIdentity();
        for (const auto& addressSpace : workspace_.addressSpaces) {
            validateAddressSpace(addressSpace);
        }
        std::sort(diagnostics_.begin(), diagnostics_.end(), diagnosticLess);
        return std::move(diagnostics_);
    }

private:
    const Workspace& workspace_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<ObjectId, std::string> ids_;

    void validateWorkspaceIdentity();
    void validateIdentity(const ObjectId& id, std::string_view name, std::string_view kind,
                          const SourceLocation& source);
    void validateAddressSpace(const AddressSpace& addressSpace);
    void validateBlock(const AddressSpace& addressSpace, const RegisterBlock& block,
                       std::vector<AddressInterval>& intervals,
                       std::vector<BlockAddressInterval>& blockIntervals);
    void validateBlockAddressIntervals(std::vector<BlockAddressInterval>& intervals);
    void validateRegister(const Register& reg);
    void validateFieldContainer(const Register& reg, const std::vector<Field>& fields,
                                std::uint64_t containerWidth, std::uint64_t absoluteLsb,
                                std::string_view containerName);
    void validateField(const Register& reg, const Field& field, std::uint64_t containerWidth,
                       std::uint64_t absoluteLsb);
    void validateAddressIntervals(std::vector<AddressInterval>& intervals);
};

void Validator::validateIdentity(const ObjectId& id, std::string_view name, std::string_view kind,
                                 const SourceLocation& source)
{
    if (id.empty()) {
        addDiagnostic(diagnostics_, missingIdentityCode, std::string(kind) + " has no stable ID.",
                      id, source);
    } else {
        const auto [iterator, inserted] = ids_.emplace(id, kind);
        if (!inserted) {
            addDiagnostic(diagnostics_, duplicateIdCode,
                          "Stable ID '" + id + "' is shared by a " + iterator->second + " and a " +
                              std::string(kind) + '.',
                          id, source);
        }
    }

    if (name.empty()) {
        addDiagnostic(diagnostics_, missingIdentityCode, std::string(kind) + " has no name.", id,
                      source);
    }
}

void Validator::validateWorkspaceIdentity()
{
    if (workspace_.id.empty()) {
        addDiagnostic(diagnostics_, missingIdentityCode, "Workspace has no stable ID.",
                      workspace_.id, {});
    } else {
        ids_.emplace(workspace_.id, "workspace");
    }
    if (workspace_.name.empty()) {
        addDiagnostic(diagnostics_, missingIdentityCode, "Workspace has no name.", workspace_.id,
                      {});
    }

    std::set<std::string, std::less<>> names;
    for (const auto& addressSpace : workspace_.addressSpaces) {
        if (!addressSpace.name.empty() && !names.insert(addressSpace.name).second) {
            addDiagnostic(diagnostics_, duplicateNameCode,
                          "Page name '" + addressSpace.name + "' is duplicated.",
                          addressSpace.id, propertySource(addressSpace, "address_space_name"));
        }
    }
}

void Validator::validateAddressSpace(const AddressSpace& addressSpace)
{
    validateIdentity(addressSpace.id, addressSpace.name, "page", addressSpace.source);
    if (addressSpace.addressWidth == 0 || addressSpace.addressWidth > 64) {
        addDiagnostic(diagnostics_, addressWidthCode,
                      "Address width must be between 1 and 64 bits.", addressSpace.id,
                      propertySource(addressSpace, "address_width"));
    } else if (addressSpace.addressWidth < 64) {
        const std::uint64_t limit = std::uint64_t{1} << addressSpace.addressWidth;
        if (addressSpace.baseAddress >= limit) {
            addDiagnostic(diagnostics_, addressRangeCode,
                          "Page base address does not fit its address width.",
                          addressSpace.id, propertySource(addressSpace, "address_space_base"));
        }
    }

    std::set<std::string, std::less<>> blockNames;
    std::vector<AddressInterval> intervals;
    std::vector<BlockAddressInterval> blockIntervals;
    for (const auto& block : addressSpace.blocks) {
        if (!block.name.empty() && !blockNames.insert(block.name).second) {
            addDiagnostic(diagnostics_, duplicateNameCode,
                          "Register-block name '" + block.name +
                              "' is duplicated in page '" + addressSpace.name + "'.",
                          block.id, propertySource(block, "block_name"));
        }
        validateBlock(addressSpace, block, intervals, blockIntervals);
    }
    validateBlockAddressIntervals(blockIntervals);
    validateAddressIntervals(intervals);
}

void Validator::validateBlock(const AddressSpace& addressSpace, const RegisterBlock& block,
                              std::vector<AddressInterval>& intervals,
                              std::vector<BlockAddressInterval>& blockIntervals)
{
    validateIdentity(block.id, block.name, "register block", block.source);
    if (block.size.has_value() && *block.size == 0) {
        addDiagnostic(diagnostics_, addressRangeCode,
                      "Register-block size must be greater than zero when specified.", block.id,
                      propertySource(block, "block_size"));
    }

    std::uint64_t blockAbsolute = 0;
    const bool blockAddressOverflow =
        addOverflow(addressSpace.baseAddress, block.baseAddress, blockAbsolute);
    if (blockAddressOverflow) {
        addDiagnostic(diagnostics_, addressRangeCode,
                      "Register-block base address overflows the 64-bit address range.", block.id,
                      propertySource(block, "block_base"));
    }
    if (!blockAddressOverflow && block.size.has_value() && *block.size > 0) {
        std::uint64_t blockLast = 0;
        if (addOverflow(blockAbsolute, *block.size - 1, blockLast)) {
            addDiagnostic(
                diagnostics_, addressRangeCode,
                "Declared register-block range overflows the 64-bit address range.",
                block.id, propertySource(block, "block_size"));
        } else {
            if (addressSpace.addressWidth > 0 &&
                addressSpace.addressWidth < 64) {
                const std::uint64_t limit =
                    std::uint64_t{1} << addressSpace.addressWidth;
                if (blockLast >= limit) {
                    addDiagnostic(
                        diagnostics_, addressRangeCode,
                        "Declared Register Block range lies outside the Page address width.",
                        block.id, propertySource(block, "block_size"));
                }
            }
            blockIntervals.push_back(
                BlockAddressInterval{blockAbsolute, blockLast, &block});
        }
    }

    std::set<std::string, std::less<>> registerNames;
    for (const auto& reg : block.registers) {
        if (!reg.name.empty() && !registerNames.insert(reg.name).second) {
            addDiagnostic(diagnostics_, duplicateNameCode,
                          "Register name '" + reg.name + "' is duplicated in block '" + block.name +
                              "'.",
                          reg.id, propertySource(reg, "name"));
        }

        validateRegister(reg);
        if (reg.width == 0 || reg.array.count == 0 || blockAddressOverflow) {
            continue;
        }

        const std::uint64_t byteWidth = (static_cast<std::uint64_t>(reg.width) + 7) / 8;
        for (std::uint32_t instance = 0; instance < reg.array.count; ++instance) {
            std::uint64_t arrayOffset = 0;
            std::uint64_t registerOffset = 0;
            std::uint64_t absoluteStart = 0;
            std::uint64_t absoluteLast = 0;
            const bool overflow = multiplyOverflow(reg.array.stride, instance, arrayOffset) ||
                                  addOverflow(reg.offset, arrayOffset, registerOffset) ||
                                  addOverflow(blockAbsolute, registerOffset, absoluteStart) ||
                                  addOverflow(absoluteStart, byteWidth - 1, absoluteLast);
            if (overflow) {
                addDiagnostic(diagnostics_, registerRangeCode,
                              "Register address calculation overflows the 64-bit address range.",
                              reg.id, propertySource(reg, "offset"));
                break;
            }

            if (block.size.has_value()) {
                std::uint64_t localLast = 0;
                if (addOverflow(registerOffset, byteWidth - 1, localLast) ||
                    localLast >= *block.size) {
                    addDiagnostic(
                        diagnostics_, registerRangeCode,
                        "Register instance lies outside the declared register-block size.", reg.id,
                        propertySource(reg, "offset"));
                }
            }

            if (addressSpace.addressWidth > 0 && addressSpace.addressWidth < 64) {
                const std::uint64_t limit = std::uint64_t{1} << addressSpace.addressWidth;
                if (absoluteLast >= limit) {
                    addDiagnostic(diagnostics_, addressRangeCode,
                                  "Register instance lies outside the Page address width.", reg.id,
                                  propertySource(reg, "offset"));
                }
            }

            intervals.push_back(AddressInterval{absoluteStart, absoluteLast, &reg, instance});
        }
    }
}

void Validator::validateBlockAddressIntervals(
    std::vector<BlockAddressInterval>& intervals)
{
    std::sort(
        intervals.begin(), intervals.end(),
        [](const BlockAddressInterval& left,
           const BlockAddressInterval& right) {
            return std::tuple{left.first, left.last, left.block->id} <
                   std::tuple{right.first, right.last, right.block->id};
        });
    if (intervals.empty()) {
        return;
    }

    const BlockAddressInterval* active = &intervals.front();
    for (std::size_t index = 1; index < intervals.size(); ++index) {
        const BlockAddressInterval& current = intervals[index];
        if (current.first <= active->last) {
            addDiagnostic(
                diagnostics_, blockOverlapCode,
                "Declared range for register block '" +
                    current.block->name +
                    "' overlaps register block '" + active->block->name + "'.",
                current.block->id,
                propertySource(*current.block, "block_base"));
        }
        if (current.last > active->last) {
            active = &current;
        }
    }
}

void Validator::validateAddressIntervals(std::vector<AddressInterval>& intervals)
{
    std::sort(intervals.begin(), intervals.end(), [](const auto& left, const auto& right) {
        return std::tuple{left.first, left.last, left.reg->id, left.instance} <
               std::tuple{right.first, right.last, right.reg->id, right.instance};
    });
    if (intervals.empty()) {
        return;
    }

    const AddressInterval* active = &intervals.front();
    for (std::size_t index = 1; index < intervals.size(); ++index) {
        const AddressInterval& current = intervals[index];
        if (current.first <= active->last) {
            std::ostringstream message;
            message << "Register '" << current.reg->name << "' instance " << current.instance
                    << " overlaps register '" << active->reg->name << "' instance "
                    << active->instance << ".";
            addDiagnostic(diagnostics_, addressOverlapCode, message.str(), current.reg->id,
                          propertySource(*current.reg, "offset"));
        }
        if (current.last > active->last) {
            active = &current;
        }
    }
}

void Validator::validateRegister(const Register& reg)
{
    validateIdentity(reg.id, reg.name, "register", reg.source);
    if (reg.width == 0) {
        addDiagnostic(diagnostics_, registerWidthCode, "Register width must be greater than zero.",
                      reg.id, propertySource(reg, "width"));
    }
    if (reg.array.count == 0) {
        addDiagnostic(diagnostics_, arrayCountCode,
                      "Register array count must be greater than zero.", reg.id,
                      propertySource(reg, "array_count"));
    }

    const std::uint64_t byteWidth = (static_cast<std::uint64_t>(reg.width) + 7) / 8;
    if (reg.array.count > 1 && reg.array.stride < byteWidth) {
        addDiagnostic(diagnostics_, strideCode,
                      "Register array stride is smaller than the register byte width.", reg.id,
                      propertySource(reg, "stride"));
    }
    if (reg.resetValue.has_value() && !reg.resetValue->fitsInBits(reg.width)) {
        addDiagnostic(diagnostics_, resetWidthCode,
                      "Register reset value does not fit the register width.", reg.id,
                      propertySource(reg, "reset"));
    }
    if (reg.initialValue.has_value() && !reg.initialValue->fitsInBits(reg.width)) {
        addDiagnostic(diagnostics_, registerInitialCode,
                      "Register initial value does not fit the register width.", reg.id,
                      propertySource(reg, "initial"));
    }
    if (reg.type == FieldType::boolean && reg.width != 1) {
        addDiagnostic(diagnostics_, fieldTypeCode,
                      "Boolean register width must be exactly one bit.", reg.id,
                      propertySource(reg, "type"));
    }

    std::set<std::string, std::less<>> enumNames;
    std::set<std::string, std::less<>> enumValues;
    for (const auto& enumValue : reg.enumValues) {
        validateIdentity(enumValue.id, enumValue.name, "enum value", enumValue.source);
        if (!enumValue.value.fitsInBits(reg.width)) {
            addDiagnostic(diagnostics_, enumWidthCode,
                          "Enum value does not fit the register width.", enumValue.id,
                          propertySource(enumValue, "value"));
        }
        if (!enumNames.insert(enumValue.name).second) {
            addDiagnostic(diagnostics_, duplicateEnumCode,
                          "Enum name '" + enumValue.name + "' is duplicated.", enumValue.id,
                          propertySource(enumValue, "name"));
        }
        const std::string valueKey = enumValue.value.toHexString(false);
        if (!enumValues.insert(valueKey).second) {
            addDiagnostic(diagnostics_, duplicateEnumCode,
                          "Enum numeric value " + enumValue.value.toHexString() + " is duplicated.",
                          enumValue.id, propertySource(enumValue, "value"));
        }
    }
    const bool enumerationLike =
        reg.type == FieldType::enumeration || reg.type == FieldType::boolean;
    if (reg.type == FieldType::enumeration && reg.enumValues.empty()) {
        addDiagnostic(diagnostics_, enumTypeCode, "Enumeration register has no enum values.",
                      reg.id, propertySource(reg, "type"));
    } else if (!enumerationLike && !reg.enumValues.empty()) {
        addDiagnostic(diagnostics_, enumTypeCode,
                      "Register has enum values but its type is not enum or bool.", reg.id,
                      propertySource(reg, "type"), DiagnosticSeverity::warning);
    }
    const auto enumContains = [&](const UnsignedValue& value) {
        return std::ranges::any_of(
            reg.enumValues, [&](const EnumValue& enumValue) { return enumValue.value == value; });
    };
    if (enumerationLike && !reg.enumValues.empty() && reg.initialValue &&
        !enumContains(*reg.initialValue)) {
        addDiagnostic(diagnostics_, enumResetCode,
                      "Register initial value is not represented by an enum value.", reg.id,
                      propertySource(reg, "initial"));
    }
    if (enumerationLike && !reg.enumValues.empty() && reg.resetValue &&
        !enumContains(*reg.resetValue)) {
        addDiagnostic(diagnostics_, enumResetCode,
                      "Register reset value is not represented by an enum value.", reg.id,
                      propertySource(reg, "reset"));
    }

    const bool numeric =
        reg.type == FieldType::signedInteger || reg.type == FieldType::unsignedInteger;
    std::optional<SignedMagnitude> minimum;
    std::optional<SignedMagnitude> maximum;
    if (reg.minimumValue) {
        minimum = parseSignedMagnitude(*reg.minimumValue);
    }
    if (reg.maximumValue) {
        maximum = parseSignedMagnitude(*reg.maximumValue);
    }
    const bool rangeSyntaxValid =
        (!reg.minimumValue || minimum.has_value()) &&
        (!reg.maximumValue || maximum.has_value());
    const bool rangeWidthValid =
        (!minimum || fitsNumericType(*minimum, reg.type, reg.width)) &&
        (!maximum || fitsNumericType(*maximum, reg.type, reg.width));
    const bool rangeOrderValid =
        !minimum || !maximum || compareSigned(*minimum, *maximum) <= 0;
    if (!rangeSyntaxValid) {
        addDiagnostic(diagnostics_, numericRangeCode,
                      "Register range bound must be a signed integer literal.", reg.id,
                      propertySource(reg, "minimum"));
    } else if ((minimum || maximum) && !numeric) {
        addDiagnostic(diagnostics_, numericRangeCode,
                      "Only signed and unsigned numeric registers may define a range.", reg.id,
                      propertySource(reg, "minimum"));
    } else if (numeric) {
        if (!rangeWidthValid) {
            addDiagnostic(diagnostics_, numericRangeCode,
                          "Numeric range does not fit the register type and width.", reg.id,
                          propertySource(reg, "minimum"));
        }
        if (!rangeOrderValid) {
            addDiagnostic(diagnostics_, numericRangeCode,
                          "Register minimum is greater than the maximum.", reg.id,
                          propertySource(reg, "minimum"));
        }
    }
    const bool configuredRangeValid =
        (reg.minimumValue || reg.maximumValue) && numeric && reg.width > 0 &&
        rangeSyntaxValid && rangeWidthValid && rangeOrderValid;
    if (configuredRangeValid && reg.initialValue && reg.initialValue->fitsInBits(reg.width) &&
        !encodedNumericValueIsInRange(*reg.initialValue, reg.type, reg.width, minimum, maximum)) {
        addDiagnostic(diagnostics_, numericRangeCode,
                      "Register initial value lies outside the configured numeric range.", reg.id,
                      propertySource(reg, "initial"));
    }
    if (configuredRangeValid && reg.resetValue && reg.resetValue->fitsInBits(reg.width) &&
        !encodedNumericValueIsInRange(*reg.resetValue, reg.type, reg.width, minimum, maximum)) {
        addDiagnostic(diagnostics_, numericRangeCode,
                      "Register reset value lies outside the configured numeric range.", reg.id,
                      propertySource(reg, "reset"), DiagnosticSeverity::warning);
    }

    if (reg.type == FieldType::structure && reg.fields.empty()) {
        addDiagnostic(diagnostics_, compoundFieldCode, "Field-type register has no fields.", reg.id,
                      propertySource(reg, "type"), DiagnosticSeverity::warning);
    } else if (reg.type != FieldType::structure && !reg.fields.empty()) {
        addDiagnostic(diagnostics_, compoundFieldCode,
                      "Register has fields but its type is not field/compound.", reg.id,
                      propertySource(reg, "type"));
    }

    if (reg.reserved) {
        if (reg.type != FieldType::reserved || reg.access != AccessMode::none ||
            !reg.enumValues.empty() || !reg.fields.empty() ||
            (reg.resetValue && !reg.resetValue->isZero())) {
            addDiagnostic(diagnostics_, reservedRegisterCode,
                          "Reserved register must have no access, no enum values, no fields, and a "
                          "zero or empty reset.",
                          reg.id, propertySource(reg, "reserved"));
        }
    }

    std::set<std::string, std::less<>> tags;
    for (const auto& tag : reg.tags) {
        const auto first = std::ranges::find_if_not(tag, [](char character) {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        });
        const bool hasBoundaryWhitespace =
            !tag.empty() &&
            (std::isspace(static_cast<unsigned char>(tag.front())) != 0 ||
             std::isspace(static_cast<unsigned char>(tag.back())) != 0);
        if (first == tag.end()) {
            addDiagnostic(diagnostics_, tagCode, "Register tag must not be empty.", reg.id,
                          propertySource(reg, "tags"));
        } else if (hasBoundaryWhitespace) {
            addDiagnostic(
                diagnostics_, tagCode,
                "Register tag '" + tag + "' must not have leading or trailing whitespace.",
                reg.id, propertySource(reg, "tags"));
        } else if (tag.find(',') != std::string::npos) {
            addDiagnostic(diagnostics_, tagCode,
                          "Register tag '" + tag + "' must not contain a comma.", reg.id,
                          propertySource(reg, "tags"));
        } else {
            std::string normalized = tag;
            std::ranges::transform(normalized, normalized.begin(), [](char character) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            });
            if (!tags.insert(normalized).second) {
                addDiagnostic(diagnostics_, tagCode, "Register tag '" + tag + "' is duplicated.",
                              reg.id, propertySource(reg, "tags"));
            }
        }
    }

    validateFieldContainer(reg, reg.fields, reg.width, 0, reg.name);
}

void Validator::validateFieldContainer(const Register& reg, const std::vector<Field>& fields,
                                       std::uint64_t containerWidth, std::uint64_t absoluteLsb,
                                       std::string_view containerName)
{
    std::set<std::string, std::less<>> fieldNames;
    std::vector<const Field*> validFields;
    validFields.reserve(fields.size());
    for (const auto& field : fields) {
        if (!field.name.empty() && !fieldNames.insert(field.name).second) {
            addDiagnostic(diagnostics_, duplicateNameCode,
                          "Field name '" + field.name + "' is duplicated in '" +
                              std::string(containerName) + "'.",
                          field.id, propertySource(field, "name"));
        }
        validateField(reg, field, containerWidth, absoluteLsb);
        if (field.msb >= field.lsb && field.msb < containerWidth) {
            validFields.push_back(&field);
        }
    }

    std::sort(validFields.begin(), validFields.end(), [](const Field* left, const Field* right) {
        return std::tuple{left->lsb, left->msb, left->id} <
               std::tuple{right->lsb, right->msb, right->id};
    });
    const Field* active = validFields.empty() ? nullptr : validFields.front();
    for (std::size_t index = 1; index < validFields.size(); ++index) {
        const Field* current = validFields[index];
        if (current->lsb <= active->msb) {
            addDiagnostic(diagnostics_, fieldOverlapCode,
                          "Field '" + current->name + "' overlaps field '" + active->name + "'.",
                          current->id, propertySource(*current, "lsb"));
        }
        if (current->msb > active->msb) {
            active = current;
        }
    }
}

void Validator::validateField(const Register& reg, const Field& field, std::uint64_t containerWidth,
                              std::uint64_t absoluteLsb)
{
    validateIdentity(field.id, field.name, "field", field.source);
    const std::uint64_t fieldWidth = field.width();
    const std::uint64_t registerLsb = absoluteLsb + field.lsb;
    if (fieldWidth == 0 || field.msb >= containerWidth) {
        addDiagnostic(diagnostics_, fieldRangeCode,
                      "Field bit range is reversed or lies outside its containing width.", field.id,
                      propertySource(field, "msb"));
    }
    if (field.type == FieldType::boolean && fieldWidth != 1) {
        addDiagnostic(diagnostics_, fieldTypeCode, "Boolean field width must be exactly one bit.",
                      field.id, propertySource(field, "type"));
    }

    if ((canRead(field.softwareAccess) && !canRead(reg.access)) ||
        (canWrite(field.softwareAccess) && !canWrite(reg.access))) {
        addDiagnostic(diagnostics_, accessConflictCode,
                      "Field software access is not permitted by the containing register access.",
                      field.id, propertySource(field, "sw_access"));
    }
    if (field.type == FieldType::reserved &&
        (field.softwareAccess != AccessMode::none || field.hardwareAccess != AccessMode::none)) {
        addDiagnostic(diagnostics_, accessConflictCode,
                      "Reserved field must not expose software or hardware access.", field.id,
                      propertySource(field, "sw_access"));
    }
    if (field.type != FieldType::reserved && field.type != FieldType::structure &&
        field.softwareAccess == AccessMode::none && field.hardwareAccess == AccessMode::none) {
        addDiagnostic(diagnostics_, accessConflictCode,
                      "Field is inaccessible from both software and hardware.", field.id,
                      propertySource(field, "sw_access"), DiagnosticSeverity::warning);
    }
    if (field.readSideEffect != ReadSideEffect::none && !canRead(field.softwareAccess)) {
        addDiagnostic(diagnostics_, sideEffectCode,
                      "Read side effect requires software-readable access.", field.id,
                      propertySource(field, "read_side_effect"));
    }
    if (field.writeSideEffect != WriteSideEffect::none && !canWrite(field.softwareAccess)) {
        addDiagnostic(diagnostics_, sideEffectCode,
                      "Write behavior requires software-writable access.", field.id,
                      propertySource(field, "write_side_effect"));
    }

    if (field.resetValue.has_value() && !field.resetValue->fitsInBits(fieldWidth)) {
        addDiagnostic(diagnostics_, resetWidthCode,
                      "Field reset value does not fit the field width.", field.id,
                      propertySource(field, "reset"));
    }
    if (field.resetValue.has_value() && reg.resetValue.has_value() && fieldWidth > 0 &&
        *field.resetValue != reg.resetValue->slice(registerLsb, fieldWidth)) {
        addDiagnostic(diagnostics_, resetMismatchCode,
                      "Field reset value does not match the corresponding register reset bits.",
                      field.id, propertySource(field, "reset"));
    }

    std::set<std::string, std::less<>> enumNames;
    std::set<std::string, std::less<>> enumValues;
    for (const auto& enumValue : field.enumValues) {
        validateIdentity(enumValue.id, enumValue.name, "enum value", enumValue.source);
        if (!enumValue.value.fitsInBits(fieldWidth)) {
            addDiagnostic(diagnostics_, enumWidthCode, "Enum value does not fit the field width.",
                          enumValue.id, propertySource(enumValue, "value"));
        }
        if (!enumNames.insert(enumValue.name).second) {
            addDiagnostic(diagnostics_, duplicateEnumCode,
                          "Enum name '" + enumValue.name + "' is duplicated.", enumValue.id,
                          propertySource(enumValue, "name"));
        }
        const std::string valueKey = enumValue.value.toHexString(false);
        if (!enumValues.insert(valueKey).second) {
            addDiagnostic(diagnostics_, duplicateEnumCode,
                          "Enum numeric value " + enumValue.value.toHexString() + " is duplicated.",
                          enumValue.id, propertySource(enumValue, "value"));
        }
    }

    const bool enumerationLike =
        field.type == FieldType::enumeration || field.type == FieldType::boolean;
    if (field.type == FieldType::enumeration && field.enumValues.empty()) {
        addDiagnostic(diagnostics_, enumTypeCode, "Enumeration field has no enum values.", field.id,
                      propertySource(field, "type"));
    } else if (!enumerationLike && !field.enumValues.empty()) {
        addDiagnostic(diagnostics_, enumTypeCode,
                      "Field has enum values but its type is not enum or bool.", field.id,
                      propertySource(field, "type"), DiagnosticSeverity::warning);
    }

    std::optional<UnsignedValue> effectiveReset = field.resetValue;
    if (!effectiveReset.has_value() && reg.resetValue.has_value() && fieldWidth > 0) {
        effectiveReset = reg.resetValue->slice(registerLsb, fieldWidth);
    }
    if (enumerationLike && !field.enumValues.empty() && effectiveReset.has_value() &&
        std::ranges::none_of(field.enumValues, [&](const EnumValue& enumValue) {
            return enumValue.value == *effectiveReset;
        })) {
        addDiagnostic(diagnostics_, enumResetCode,
                      "Field reset value is not represented by an enum value.", field.id,
                      field.resetValue.has_value() ? propertySource(field, "reset")
                                                   : propertySource(reg, "reset"));
    }

    const bool numeric =
        field.type == FieldType::signedInteger || field.type == FieldType::unsignedInteger;
    std::optional<SignedMagnitude> minimum;
    std::optional<SignedMagnitude> maximum;
    if (field.minimumValue) {
        minimum = parseSignedMagnitude(*field.minimumValue);
    }
    if (field.maximumValue) {
        maximum = parseSignedMagnitude(*field.maximumValue);
    }
    const bool rangeSyntaxValid =
        (!field.minimumValue || minimum.has_value()) &&
        (!field.maximumValue || maximum.has_value());
    const bool rangeWidthValid =
        (!minimum || fitsNumericType(*minimum, field.type, fieldWidth)) &&
        (!maximum || fitsNumericType(*maximum, field.type, fieldWidth));
    const bool rangeOrderValid =
        !minimum || !maximum || compareSigned(*minimum, *maximum) <= 0;
    if (!rangeSyntaxValid) {
        addDiagnostic(diagnostics_, numericRangeCode,
                      "Numeric range bound must be a signed integer literal.", field.id,
                      propertySource(field, "minimum"));
    } else if ((minimum || maximum) && !numeric) {
        addDiagnostic(diagnostics_, numericRangeCode,
                      "Only signed and unsigned numeric fields may define a range.", field.id,
                      propertySource(field, "minimum"));
    } else if (numeric) {
        if (!rangeWidthValid) {
            addDiagnostic(diagnostics_, numericRangeCode,
                          "Numeric range does not fit the field type and width.", field.id,
                          propertySource(field, "minimum"));
        }
        if (!rangeOrderValid) {
            addDiagnostic(diagnostics_, numericRangeCode,
                          "Numeric minimum is greater than the maximum.", field.id,
                          propertySource(field, "minimum"));
        }
    }
    const bool configuredRangeValid =
        (field.minimumValue || field.maximumValue) && numeric && fieldWidth > 0 &&
        rangeSyntaxValid && rangeWidthValid && rangeOrderValid;
    if (configuredRangeValid && effectiveReset && effectiveReset->fitsInBits(fieldWidth) &&
        !encodedNumericValueIsInRange(*effectiveReset, field.type, fieldWidth, minimum, maximum)) {
        addDiagnostic(
            diagnostics_, numericRangeCode,
            "Field reset value lies outside the configured numeric range.", field.id,
            field.resetValue ? propertySource(field, "reset") : propertySource(reg, "reset"),
            DiagnosticSeverity::warning);
    }

    if (field.type == FieldType::structure && field.members.empty()) {
        addDiagnostic(diagnostics_, compoundFieldCode, "Compound field has no member fields.",
                      field.id, propertySource(field, "type"));
    } else if (field.type != FieldType::structure && !field.members.empty()) {
        addDiagnostic(diagnostics_, compoundFieldCode,
                      "Field has member fields but its type is not field/compound.", field.id,
                      propertySource(field, "type"));
    }
    if (!field.members.empty()) {
        validateFieldContainer(reg, field.members, fieldWidth, registerLsb, field.name);
    }
}

} // namespace

std::vector<Diagnostic> validateWorkspace(const Workspace& workspace)
{
    return Validator(workspace).run();
}

} // namespace regmap
