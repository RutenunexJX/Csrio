#pragma once

#include "regmap/core/unsigned_value.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace regmap {

using ObjectId = std::string;

struct SourceLocation {
    std::filesystem::path workbook;
    std::string sheet;
    std::optional<std::uint32_t> row;
    std::optional<std::uint32_t> column;
    std::string cell;

    [[nodiscard]] bool empty() const noexcept;
};

using PropertySources = std::map<std::string, SourceLocation, std::less<>>;

enum class AccessMode {
    none,
    readOnly,
    writeOnly,
    readWrite,
};

enum class FieldType {
    bits,
    boolean,
    unsignedInteger,
    signedInteger,
    enumeration,
    structure,
    reserved,
};

enum class ReadSideEffect {
    none,
    clear,
    set,
};

enum class WriteSideEffect {
    none,
    write,
    oneToClear,
    oneToSet,
    zeroToClear,
    zeroToSet,
    toggle,
};

struct EnumValue {
    ObjectId id;
    std::string name;
    UnsignedValue value;
    std::string description;
    SourceLocation source;
    PropertySources propertySources;
};

struct Field {
    ObjectId id;
    std::string name;
    std::uint32_t msb{0};
    std::uint32_t lsb{0};
    FieldType type{FieldType::bits};
    AccessMode softwareAccess{AccessMode::readWrite};
    AccessMode hardwareAccess{AccessMode::none};
    std::optional<UnsignedValue> resetValue;
    ReadSideEffect readSideEffect{ReadSideEffect::none};
    WriteSideEffect writeSideEffect{WriteSideEffect::write};
    std::string resetDomain;
    std::string description;
    std::optional<std::string> minimumValue;
    std::optional<std::string> maximumValue;
    std::vector<EnumValue> enumValues;
    std::vector<Field> members;
    SourceLocation source;
    PropertySources propertySources;

    [[nodiscard]] std::uint64_t width() const noexcept;
};

struct RegisterArray {
    std::uint32_t count{1};
    std::uint64_t stride{0};
};

struct Register {
    ObjectId id;
    std::string name;
    std::uint64_t offset{0};
    std::uint32_t width{32};
    RegisterArray array;
    FieldType type{FieldType::unsignedInteger};
    std::optional<std::string> minimumValue;
    std::optional<std::string> maximumValue;
    std::optional<UnsignedValue> initialValue;
    std::optional<UnsignedValue> resetValue;
    AccessMode access{AccessMode::readWrite};
    bool reserved{false};
    std::vector<std::string> tags;
    std::string description;
    std::vector<EnumValue> enumValues;
    std::vector<Field> fields;
    SourceLocation source;
    PropertySources propertySources;
};

struct RegisterBlock {
    ObjectId id;
    std::string name;
    std::uint64_t baseAddress{0};
    std::optional<std::uint64_t> size;
    std::string description;
    std::vector<Register> registers;
    SourceLocation source;
    PropertySources propertySources;
};

struct AddressSpace {
    ObjectId id;
    std::string name;
    std::uint64_t baseAddress{0};
    std::uint32_t addressWidth{32};
    std::string description;
    std::vector<RegisterBlock> blocks;
    SourceLocation source;
    PropertySources propertySources;
};

struct Workspace {
    ObjectId id;
    std::string name;
    std::filesystem::path manifestPath;
    std::vector<AddressSpace> addressSpaces;
};

} // namespace regmap
