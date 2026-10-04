#include "regmap/core/generation.hpp"

#include "regmap/core/xlsx_export.hpp"

#include "path_identity.hpp"
#include "atomic_file_writer.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view writeFailureCode = "RM4000";
constexpr std::string_view symbolCollisionCode = "RM4001";
constexpr std::string_view addressOverflowCode = "RM4002";
[[nodiscard]] std::string outputWriteFailureMessage(const GeneratedArtifact& artifact,
                                                    std::string_view operation,
                                                    const QString& error)
{
    std::string message = "Cannot " + std::string(operation) + " generated output '" +
                          artifact.path.string() + "'";
    if (!error.isEmpty()) {
        message += ": " + error.toUtf8().toStdString();
    }
    message += '.';
    if (artifact.kind == GenerationTargetKind::xlsx) {
        message +=
            " The XLSX file may be open in Excel; close it and generate again.";
    }
    return message;
}

template <typename Value, typename Compare>
[[nodiscard]] std::vector<const Value*> sortedPointers(const std::vector<Value>& values,
                                                       Compare compare)
{
    std::vector<const Value*> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(&value);
    }
    std::sort(result.begin(), result.end(),
              [&](const Value* left, const Value* right) { return compare(*left, *right); });
    return result;
}

[[nodiscard]] std::string identifier(std::string_view value, bool uppercase)
{
    std::string result;
    result.reserve(value.size());
    bool previousUnderscore = false;
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        char output = '_';
        if (std::isalnum(character) != 0 || character == '_') {
            output =
                static_cast<char>(uppercase ? std::toupper(character) : std::tolower(character));
        }
        if (output == '_') {
            if (previousUnderscore) {
                continue;
            }
            previousUnderscore = true;
        } else {
            previousUnderscore = false;
        }
        result.push_back(output);
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) {
        result = uppercase ? "UNNAMED" : "unnamed";
    }
    if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    return result;
}

[[nodiscard]] std::string symbol(const AddressSpace& addressSpace, const RegisterBlock& block,
                                 const Register& reg)
{
    return identifier(addressSpace.name, true) + '_' + identifier(block.name, true) + '_' +
           identifier(reg.name, true);
}

[[nodiscard]] std::string fieldSymbol(std::string_view registerSymbol, const Field& field)
{
    return std::string(registerSymbol) + '_' + identifier(field.name, true);
}

[[nodiscard]] std::optional<std::uint64_t> addAddress(std::uint64_t first,
                                                      std::uint64_t second) noexcept
{
    if (second > std::numeric_limits<std::uint64_t>::max() - first) {
        return std::nullopt;
    }
    return first + second;
}

[[nodiscard]] std::optional<std::uint64_t> absoluteAddress(const AddressSpace& addressSpace,
                                                           const RegisterBlock& block,
                                                           const Register& reg) noexcept
{
    const auto blockAddress = addAddress(addressSpace.baseAddress, block.baseAddress);
    return blockAddress ? addAddress(*blockAddress, reg.offset) : std::nullopt;
}

[[nodiscard]] std::string_view accessText(AccessMode access) noexcept
{
    switch (access) {
    case AccessMode::none:
        return "none";
    case AccessMode::readOnly:
        return "ro";
    case AccessMode::writeOnly:
        return "wo";
    case AccessMode::readWrite:
        return "rw";
    }
    return "unknown";
}

[[nodiscard]] std::string valueTypeText(FieldType type, std::uint64_t width)
{
    switch (type) {
    case FieldType::bits:
        return "bits";
    case FieldType::boolean:
        return "bool";
    case FieldType::unsignedInteger:
        return "uint" + std::to_string(width);
    case FieldType::signedInteger:
        return "int" + std::to_string(width);
    case FieldType::enumeration:
        return "enum";
    case FieldType::structure:
        return "field";
    case FieldType::reserved:
        return "reserved";
    }
    return "unknown";
}

[[nodiscard]] std::string fieldTypeText(const Field& field)
{
    return valueTypeText(field.type, field.width());
}

[[nodiscard]] std::string option(const GenerationTargetConfig& target, std::string_view name,
                                 std::string fallback)
{
    const auto iterator = target.options.find(name);
    return iterator == target.options.end() ? std::move(fallback) : iterator->second;
}

[[nodiscard]] std::string cUnsignedLiteral(const UnsignedValue& value)
{
    return "UINT64_C(" + value.toHexString() + ')';
}

[[nodiscard]] std::string cUnsignedLiteral(std::uint64_t value)
{
    return cUnsignedLiteral(UnsignedValue(value));
}

[[nodiscard]] std::string escapeMarkdown(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character == '|') {
            result += "\\|";
        } else if (character == '\r' || character == '\n') {
            result.push_back(' ');
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] auto orderedAddressSpaces(const Workspace& workspace)
{
    return sortedPointers(workspace.addressSpaces, [](const auto& left, const auto& right) {
        return std::tuple{left.baseAddress, left.id} < std::tuple{right.baseAddress, right.id};
    });
}

[[nodiscard]] auto orderedBlocks(const AddressSpace& addressSpace)
{
    return sortedPointers(addressSpace.blocks, [](const auto& left, const auto& right) {
        return std::tuple{left.baseAddress, left.id} < std::tuple{right.baseAddress, right.id};
    });
}

[[nodiscard]] auto orderedRegisters(const RegisterBlock& block)
{
    return sortedPointers(block.registers, [](const auto& left, const auto& right) {
        return std::tuple{left.offset, left.id} < std::tuple{right.offset, right.id};
    });
}

[[nodiscard]] auto orderedFields(const Register& reg)
{
    return sortedPointers(reg.fields, [](const auto& left, const auto& right) {
        return std::tuple{left.lsb, left.msb, left.id} < std::tuple{right.lsb, right.msb, right.id};
    });
}

[[nodiscard]] auto orderedFields(const std::vector<Field>& fields)
{
    return sortedPointers(fields, [](const auto& left, const auto& right) {
        return std::tuple{left.lsb, left.msb, left.id} < std::tuple{right.lsb, right.msb, right.id};
    });
}

[[nodiscard]] auto orderedEnums(const std::vector<EnumValue>& values)
{
    return sortedPointers(values, [](const auto& left, const auto& right) {
        return std::tuple{left.value.bitWidth(), left.value.toHexString(false), left.id} <
               std::tuple{right.value.bitWidth(), right.value.toHexString(false), right.id};
    });
}

[[nodiscard]] auto orderedEnums(const Field& field) { return orderedEnums(field.enumValues); }

[[nodiscard]] auto orderedEnums(const Register& reg) { return orderedEnums(reg.enumValues); }

void addDiagnostic(std::vector<Diagnostic>& diagnostics, std::string_view code, std::string message,
                   const ObjectId& objectId, SourceLocation source)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.objectId = objectId;
    diagnostic.source = std::move(source);
    diagnostics.push_back(std::move(diagnostic));
}

struct CMacroDefinition {
    std::string name;
    const ObjectId* objectId;
    const SourceLocation* source;
    const void* owner;
};

template <typename Owner>
void addCMacro(std::vector<CMacroDefinition>& definitions, std::string name, const Owner& owner)
{
    definitions.push_back(
        CMacroDefinition{std::move(name), &owner.id, &owner.source, &owner});
}

template <typename Owner>
void addCMacro(std::vector<CMacroDefinition>& definitions, std::string_view prefix,
               std::string_view suffix, const Owner& owner)
{
    addCMacro(definitions, std::string(prefix) + std::string(suffix), owner);
}

void collectCFieldMacros(std::vector<CMacroDefinition>& definitions, const Field& field,
                         std::string_view parentSymbol, std::uint64_t parentLsb)
{
    const std::uint64_t width = field.width();
    if (width == 0) {
        return;
    }
    const std::string currentFieldSymbol = fieldSymbol(parentSymbol, field);
    const std::uint64_t absoluteLsb = parentLsb + field.lsb;
    addCMacro(definitions, currentFieldSymbol, "_LSB", field);
    addCMacro(definitions, currentFieldSymbol, "_WIDTH", field);
    const UnsignedValue mask = UnsignedValue::bitMask(absoluteLsb, width);
    addCMacro(definitions, currentFieldSymbol,
              mask.fitsInBits(64) ? "_MASK" : "_MASK_HEX", field);
    if (field.minimumValue) {
        addCMacro(definitions, currentFieldSymbol, "_MIN", field);
    }
    if (field.maximumValue) {
        addCMacro(definitions, currentFieldSymbol, "_MAX", field);
    }
    if (field.type == FieldType::boolean && field.enumValues.empty()) {
        addCMacro(definitions, currentFieldSymbol, "_FALSE", field);
        addCMacro(definitions, currentFieldSymbol, "_TRUE", field);
    }
    for (const EnumValue* enumValue : orderedEnums(field)) {
        addCMacro(definitions,
                  currentFieldSymbol + '_' + identifier(enumValue->name, true), *enumValue);
    }
    for (const Field* member : orderedFields(field.members)) {
        collectCFieldMacros(definitions, *member, currentFieldSymbol, absoluteLsb);
    }
}

void validateCHeaderMacros(const Workspace& workspace, std::string_view guard,
                           std::vector<Diagnostic>& diagnostics)
{
    std::vector<CMacroDefinition> definitions;
    for (const AddressSpace* addressSpace : orderedAddressSpaces(workspace)) {
        for (const RegisterBlock* block : orderedBlocks(*addressSpace)) {
            for (const Register* reg : orderedRegisters(*block)) {
                if (reg->reserved) {
                    continue;
                }
                const std::string regSymbol = symbol(*addressSpace, *block, *reg);
                addCMacro(definitions, regSymbol, "_ADDR", *reg);
                addCMacro(definitions, regSymbol, "_OFFSET", *reg);
                addCMacro(definitions, regSymbol, "_WIDTH", *reg);
                if (reg->minimumValue) {
                    addCMacro(definitions, regSymbol, "_MIN", *reg);
                }
                if (reg->maximumValue) {
                    addCMacro(definitions, regSymbol, "_MAX", *reg);
                }
                if (reg->initialValue) {
                    addCMacro(definitions, regSymbol,
                              reg->initialValue->fitsInBits(64) ? "_INITIAL" : "_INITIAL_HEX",
                              *reg);
                }
                if (reg->resetValue) {
                    addCMacro(definitions, regSymbol,
                              reg->resetValue->fitsInBits(64) ? "_RESET" : "_RESET_HEX", *reg);
                }
                if (reg->type == FieldType::boolean && reg->enumValues.empty()) {
                    addCMacro(definitions, regSymbol, "_FALSE", *reg);
                    addCMacro(definitions, regSymbol, "_TRUE", *reg);
                }
                for (const EnumValue* enumValue : orderedEnums(*reg)) {
                    addCMacro(definitions,
                              regSymbol + '_' + identifier(enumValue->name, true), *enumValue);
                }
                for (const Field* field : orderedFields(*reg)) {
                    collectCFieldMacros(definitions, *field, regSymbol, 0);
                }
            }
        }
    }

    std::set<std::string, std::less<>> symbols;
    symbols.insert(std::string(guard));
    std::set<const void*> reportedOwners;
    for (const CMacroDefinition& definition : definitions) {
        if (symbols.insert(definition.name).second ||
            !reportedOwners.insert(definition.owner).second) {
            continue;
        }
        addDiagnostic(
            diagnostics, symbolCollisionCode,
            "Generated C macro '" + definition.name +
                "' is defined more than once after name normalization. Rename the conflicting "
                "Register, Field, or Enum value.",
            *definition.objectId, *definition.source);
    }
}

void generateCField(std::ostringstream& output, const Field& field, std::string_view parentSymbol,
                    std::uint64_t parentLsb)
{
    const std::uint64_t width = field.width();
    if (width == 0) {
        return;
    }
    const std::string currentFieldSymbol = fieldSymbol(parentSymbol, field);
    const std::uint64_t absoluteLsb = parentLsb + field.lsb;
    const UnsignedValue mask = UnsignedValue::bitMask(absoluteLsb, width);
    output << "#define " << currentFieldSymbol << "_LSB " << absoluteLsb << "u\n#define "
           << currentFieldSymbol << "_WIDTH " << width << "u\n";
    if (mask.fitsInBits(64)) {
        output << "#define " << currentFieldSymbol << "_MASK " << cUnsignedLiteral(mask) << "\n";
    } else {
        output << "#define " << currentFieldSymbol << "_MASK_HEX \"" << mask.toHexString()
               << "\"\n";
    }
    if (field.minimumValue) {
        output << "#define " << currentFieldSymbol << "_MIN (" << *field.minimumValue << ")\n";
    }
    if (field.maximumValue) {
        output << "#define " << currentFieldSymbol << "_MAX (" << *field.maximumValue << ")\n";
    }
    if (field.type == FieldType::boolean && field.enumValues.empty()) {
        output << "#define " << currentFieldSymbol << "_FALSE UINT64_C(0)\n"
               << "#define " << currentFieldSymbol << "_TRUE UINT64_C(1)\n";
    }
    for (const EnumValue* enumValue : orderedEnums(field)) {
        output << "#define " << currentFieldSymbol << '_' << identifier(enumValue->name, true)
               << ' ';
        if (enumValue->value.fitsInBits(64)) {
            output << cUnsignedLiteral(enumValue->value);
        } else {
            output << '"' << enumValue->value.toHexString() << '"';
        }
        output << '\n';
    }
    for (const Field* member : orderedFields(field.members)) {
        generateCField(output, *member, currentFieldSymbol, absoluteLsb);
    }
}

[[nodiscard]] std::string joinedTags(const std::vector<std::string>& tags)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << tags[index];
    }
    return output.str();
}

void generateMarkdownFields(std::ostringstream& output, const std::vector<Field>& fields,
                            std::string_view parentPath, std::uint64_t parentLsb,
                            std::uint64_t registerWidth,
                            const std::optional<UnsignedValue>& registerReset)
{
    for (const Field* field : orderedFields(fields)) {
        const std::uint64_t absoluteLsb = parentLsb + field->lsb;
        const std::uint64_t absoluteMsb = parentLsb + field->msb;
        const std::string path =
            parentPath.empty() ? field->name : std::string(parentPath) + '.' + field->name;
        output << "| " << absoluteMsb;
        if (absoluteMsb != absoluteLsb) {
            output << ':' << absoluteLsb;
        }
        output << " | " << escapeMarkdown(path) << " | " << fieldTypeText(*field) << " | "
               << accessText(field->softwareAccess) << " | " << accessText(field->hardwareAccess)
               << " | ";
        const bool resetRangeValid = absoluteLsb >= parentLsb && field->width() > 0 &&
            absoluteLsb <= registerWidth && field->width() <= registerWidth - absoluteLsb;
        const auto effectiveReset = registerReset.has_value() && resetRangeValid
            ? std::optional<UnsignedValue>{registerReset->slice(absoluteLsb, field->width())}
            : std::nullopt;
        if (effectiveReset.has_value()) {
            output << '`' << effectiveReset->toHexString() << '`';
        } else {
            output << "—";
        }
        output << " | ";
        if (field->minimumValue || field->maximumValue) {
            output << escapeMarkdown(field->minimumValue.value_or("—")) << " … "
                   << escapeMarkdown(field->maximumValue.value_or("—"));
        } else {
            output << "—";
        }
        output << " | " << escapeMarkdown(field->description) << " |\n";
        generateMarkdownFields(output, field->members, path, absoluteLsb, registerWidth,
                               registerReset);
    }
}

void generateMarkdownEnums(std::ostringstream& output, const std::vector<Field>& fields,
                           std::string_view parentPath)
{
    for (const Field* field : orderedFields(fields)) {
        const std::string path =
            parentPath.empty() ? field->name : std::string(parentPath) + '.' + field->name;
        if (!field->enumValues.empty()) {
            output << "Enum values for **" << escapeMarkdown(path) << "**:\n\n"
                   << "| Name | Value | Description |\n|---|---:|---|\n";
            for (const EnumValue* enumValue : orderedEnums(*field)) {
                output << "| " << escapeMarkdown(enumValue->name) << " | `"
                       << enumValue->value.toHexString() << "` | "
                       << escapeMarkdown(enumValue->description) << " |\n";
            }
            output << '\n';
        }
        generateMarkdownEnums(output, field->members, path);
    }
}

[[nodiscard]] std::string generateCHeader(const Workspace& workspace,
                                          const GenerationTargetConfig& target,
                                          std::vector<Diagnostic>& diagnostics)
{
    const std::string guard = identifier(option(target, "guard", workspace.name + "_regs_h"), true);
    validateCHeaderMacros(workspace, guard, diagnostics);
    std::ostringstream output;
    output << "/* Generated by Csrio. Do not edit. */\n"
           << "#ifndef " << guard << "\n#define " << guard << "\n\n#include <stdint.h>\n\n";

    for (const AddressSpace* addressSpace : orderedAddressSpaces(workspace)) {
        for (const RegisterBlock* block : orderedBlocks(*addressSpace)) {
            for (const Register* reg : orderedRegisters(*block)) {
                if (reg->reserved) {
                    continue;
                }
                const std::string regSymbol = symbol(*addressSpace, *block, *reg);
                const auto address = absoluteAddress(*addressSpace, *block, *reg);
                if (!address) {
                    addDiagnostic(diagnostics, addressOverflowCode,
                                  "Cannot generate an address that exceeds 64 bits.", reg->id,
                                  reg->source);
                    continue;
                }

                output << "/* Type: " << valueTypeText(reg->type, reg->width) << " */\n"
                       << "#define " << regSymbol << "_ADDR " << cUnsignedLiteral(*address)
                       << "\n#define " << regSymbol << "_OFFSET " << cUnsignedLiteral(reg->offset)
                       << "\n#define " << regSymbol << "_WIDTH " << reg->width << "u\n";
                if (reg->minimumValue) {
                    output << "#define " << regSymbol << "_MIN (" << *reg->minimumValue << ")\n";
                }
                if (reg->maximumValue) {
                    output << "#define " << regSymbol << "_MAX (" << *reg->maximumValue << ")\n";
                }
                if (reg->initialValue.has_value()) {
                    if (reg->initialValue->fitsInBits(64)) {
                        output << "#define " << regSymbol << "_INITIAL "
                               << cUnsignedLiteral(*reg->initialValue) << "\n";
                    } else {
                        output << "#define " << regSymbol << "_INITIAL_HEX \""
                               << reg->initialValue->toHexString() << "\"\n";
                    }
                }
                if (reg->resetValue.has_value()) {
                    if (reg->resetValue->fitsInBits(64)) {
                        output << "#define " << regSymbol << "_RESET "
                               << cUnsignedLiteral(*reg->resetValue) << "\n";
                    } else {
                        output << "#define " << regSymbol << "_RESET_HEX \""
                               << reg->resetValue->toHexString() << "\"\n";
                    }
                }
                if (reg->type == FieldType::boolean && reg->enumValues.empty()) {
                    output << "#define " << regSymbol << "_FALSE UINT64_C(0)\n"
                           << "#define " << regSymbol << "_TRUE UINT64_C(1)\n";
                }
                for (const EnumValue* enumValue : orderedEnums(*reg)) {
                    output << "#define " << regSymbol << '_' << identifier(enumValue->name, true)
                           << ' ';
                    if (enumValue->value.fitsInBits(64)) {
                        output << cUnsignedLiteral(enumValue->value);
                    } else {
                        output << '"' << enumValue->value.toHexString() << '"';
                    }
                    output << '\n';
                }

                for (const Field* field : orderedFields(*reg)) {
                    generateCField(output, *field, regSymbol, 0);
                }
                output << '\n';
            }
        }
    }
    output << "#endif /* " << guard << " */\n";
    return output.str();
}

[[nodiscard]] std::string generateMarkdown(const Workspace& workspace,
                                           const GenerationTargetConfig& target,
                                           std::vector<Diagnostic>& diagnostics)
{
    const std::string title = option(target, "title", workspace.name);
    std::ostringstream output;
    output << "# " << escapeMarkdown(title) << "\n\n"
           << "> Generated by Csrio. Do not edit.\n\n";

    for (const AddressSpace* addressSpace : orderedAddressSpaces(workspace)) {
        output << "## Page: " << escapeMarkdown(addressSpace->name) << "\n\n"
               << "Page base address: `" << UnsignedValue(addressSpace->baseAddress).toHexString()
               << "`; address width: " << addressSpace->addressWidth << " bits.\n\n";
        for (const RegisterBlock* block : orderedBlocks(*addressSpace)) {
            output << "### Register block: " << escapeMarkdown(block->name) << "\n\n"
                   << "Block base: `" << UnsignedValue(block->baseAddress).toHexString() << "`.\n\n"
                   << "| Register | Address | Offset | Width | Type | Access | Range | Initial | "
                      "Reset | Tags | State |\n"
                   << "|---|---:|---:|---:|---|---|---|---:|---:|---|---|\n";
            for (const Register* reg : orderedRegisters(*block)) {
                const auto address = absoluteAddress(*addressSpace, *block, *reg);
                if (!address) {
                    addDiagnostic(diagnostics, addressOverflowCode,
                                  "Cannot generate an address that exceeds 64 bits.", reg->id,
                                  reg->source);
                    continue;
                }
                output << "| " << escapeMarkdown(reg->name) << " | `"
                       << UnsignedValue(*address).toHexString() << "` | `"
                       << UnsignedValue(reg->offset).toHexString() << "` | " << reg->width << " | "
                       << valueTypeText(reg->type, reg->width) << " | " << accessText(reg->access)
                       << " | ";
                if (reg->minimumValue || reg->maximumValue) {
                    output << escapeMarkdown(reg->minimumValue.value_or("—")) << " … "
                           << escapeMarkdown(reg->maximumValue.value_or("—"));
                } else {
                    output << "—";
                }
                output << " | ";
                if (reg->initialValue.has_value()) {
                    output << '`' << reg->initialValue->toHexString() << '`';
                } else {
                    output << "—";
                }
                output << " | ";
                if (reg->resetValue.has_value()) {
                    output << '`' << reg->resetValue->toHexString() << '`';
                } else {
                    output << "—";
                }
                output << " | " << escapeMarkdown(joinedTags(reg->tags)) << " | "
                       << (reg->reserved ? "reserved" : "active") << " |\n";
            }
            output << '\n';

            for (const Register* reg : orderedRegisters(*block)) {
                output << "#### " << escapeMarkdown(reg->name) << "\n\n";
                if (!reg->description.empty()) {
                    output << escapeMarkdown(reg->description) << "\n\n";
                }
                if (reg->reserved) {
                    output << "Reserved address slot.\n\n";
                    continue;
                }
                if (reg->type == FieldType::boolean || !reg->enumValues.empty()) {
                    output << "Values for **" << escapeMarkdown(reg->name) << "**:\n\n"
                           << "| Name | Value | Description |\n|---|---:|---|\n";
                    if (reg->type == FieldType::boolean && reg->enumValues.empty()) {
                        output << "| FALSE | `0x0` | Implicit |\n"
                               << "| TRUE | `0x1` | Implicit |\n";
                    } else {
                        for (const EnumValue* enumValue : orderedEnums(*reg)) {
                            output << "| " << escapeMarkdown(enumValue->name) << " | `"
                                   << enumValue->value.toHexString() << "` | "
                                   << escapeMarkdown(enumValue->description) << " |\n";
                        }
                    }
                    output << '\n';
                }
                if (!reg->fields.empty()) {
                    output << "| Bits | Field | Type | SW | HW | Reset | Range | Description |\n"
                           << "|---:|---|---|---|---|---:|---|---|\n";
                    generateMarkdownFields(output, reg->fields, {}, 0, reg->width,
                                           reg->resetValue);
                    output << '\n';
                    generateMarkdownEnums(output, reg->fields, {});
                }
            }
        }
    }
    return output.str();
}

[[nodiscard]] QString fromPath(const std::filesystem::path& value)
{
    return QString::fromStdWString(value.wstring());
}

} // namespace

bool GenerationResult::hasErrors() const noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

GenerationResult generateArtifacts(const Workspace& workspace, const ProjectManifest& manifest)
{
    GenerationResult result;
    std::set<detail::PathIdentity> paths;
    for (const auto& target : manifest.targets) {
        const std::string displayPath = target.path.resolved.generic_string();
        if (!paths.insert(detail::pathIdentity(target.path.resolved)).second) {
            SourceLocation source;
            source.workbook = manifest.manifestPath;
            addDiagnostic(result.diagnostics, symbolCollisionCode,
                          "Multiple generation targets resolve to '" + displayPath + "'.", {},
                          std::move(source));
            continue;
        }
        if (detail::samePathIdentity(target.path.resolved, manifest.manifestPath) ||
            detail::samePathIdentity(target.path.resolved, manifest.rtl.path.resolved)) {
            SourceLocation source;
            source.workbook = manifest.manifestPath;
            addDiagnostic(result.diagnostics, symbolCollisionCode,
                          "Generated output would overwrite a project source file: '" +
                              displayPath + "'.",
                          {}, std::move(source));
            continue;
        }

        GeneratedArtifact artifact;
        artifact.kind = target.kind;
        artifact.path = target.path.resolved;
        switch (target.kind) {
        case GenerationTargetKind::xlsx: {
            auto workbook = exportReadOnlyWorkbook(workspace);
            artifact.binaryContent = std::move(workbook.bytes);
            std::move(workbook.diagnostics.begin(), workbook.diagnostics.end(),
                      std::back_inserter(result.diagnostics));
            break;
        }
        case GenerationTargetKind::cHeader:
            artifact.content = generateCHeader(workspace, target, result.diagnostics);
            break;
        case GenerationTargetKind::markdown:
            artifact.content = generateMarkdown(workspace, target, result.diagnostics);
            break;
        }
        result.artifacts.push_back(std::move(artifact));
    }
    return result;
}

GeneratedArtifactInspection inspectGeneratedArtifact(
    const GeneratedArtifact& artifact)
{
    GeneratedArtifactInspection result;
    const QString path = fromPath(artifact.path);
    const QFileInfo information(path);
    if (!information.exists()) {
        return result;
    }
    result.readOnly =
        (information.permissions() &
         (QFileDevice::WriteOwner |
          QFileDevice::WriteUser |
          QFileDevice::WriteGroup |
          QFileDevice::WriteOther)) == 0;
    if (!information.isFile()) {
        result.state = GeneratedArtifactState::unreadable;
        return result;
    }

    QFile file(path);
    const QIODevice::OpenMode mode =
        artifact.isBinary()
            ? QIODevice::ReadOnly
            : QIODevice::ReadOnly | QIODevice::Text;
    if (!file.open(mode)) {
        result.state = GeneratedArtifactState::unreadable;
        return result;
    }
    const QByteArray expected =
        artifact.isBinary()
            ? QByteArray(
                  reinterpret_cast<const char*>(
                      artifact.binaryContent.data()),
                  static_cast<qsizetype>(
                      artifact.binaryContent.size()))
            : QByteArray(
                  artifact.content.data(),
                  static_cast<qsizetype>(
                      artifact.content.size()));
    if (artifact.isBinary() &&
        file.size() != expected.size()) {
        result.state = GeneratedArtifactState::modified;
        return result;
    }
    result.state =
        file.readAll() == expected
            ? GeneratedArtifactState::current
            : GeneratedArtifactState::modified;
    return result;
}

std::vector<Diagnostic> writeGeneratedArtifacts(const std::vector<GeneratedArtifact>& artifacts)
{
    std::vector<Diagnostic> diagnostics;
    for (const auto& artifact : artifacts) {
        const QString path = fromPath(artifact.path);
        const QFileInfo information(path);
        QDir directory;
        if (!directory.mkpath(information.absolutePath())) {
            SourceLocation source;
            source.workbook = artifact.path;
            addDiagnostic(diagnostics, writeFailureCode,
                          "Cannot create output directory for '" + artifact.path.string() + "'.",
                          {}, std::move(source));
            continue;
        }

        const bool outputExisted =
            information.exists();
        if (outputExisted) {
            const auto inspection =
                inspectGeneratedArtifact(artifact);
            if (inspection.contentCurrent()) {
                if (!inspection.readOnly &&
                    !QFile::setPermissions(
                        path,
                        QFileDevice::ReadOwner |
                            QFileDevice::ReadGroup |
                            QFileDevice::ReadOther)) {
                    SourceLocation source;
                    source.workbook = artifact.path;
                    addDiagnostic(
                        diagnostics,
                        writeFailureCode,
                        "Generated output is current, but could not be marked read-only: '" +
                            artifact.path.string() + "'.",
                        {},
                        std::move(source));
                }
                continue;
            }
        }
        const detail::AtomicFileWriter writer(path);
        const QFileDevice::Permissions
            originalPermissions =
                information.permissions();
        const auto restorePermissions =
            [&]() {
                if (outputExisted && writer.destinationUnchanged()) {
                    QFile::setPermissions(
                        path,
                        originalPermissions);
                }
            };
        if (outputExisted && writer.destinationUnchanged()) {
            QFile::setPermissions(
                path,
                originalPermissions |
                    QFileDevice::WriteOwner);
        }

        const QIODevice::OpenMode mode =
            artifact.isBinary() ? QIODevice::WriteOnly : QIODevice::WriteOnly | QIODevice::Text;
        const QByteArray bytes =
            artifact.isBinary()
                ? QByteArray(reinterpret_cast<const char*>(artifact.binaryContent.data()),
                             static_cast<qsizetype>(artifact.binaryContent.size()))
                : QByteArray(artifact.content.data(),
                             static_cast<qsizetype>(artifact.content.size()));

        const auto written = writer.write(bytes, mode);
        if (!written.committed) {
            SourceLocation source;
            source.workbook = artifact.path;
            addDiagnostic(
                diagnostics,
                writeFailureCode,
                outputWriteFailureMessage(
                    artifact,
                    std::string_view(written.operation) == "commit" ||
                            std::string_view(written.operation) == "changed"
                        ? "replace" : written.operation,
                    written.errorText),
                {},
                std::move(source));
            restorePermissions();
            continue;
        }
        if (!QFile::setPermissions(
                path,
                QFileDevice::ReadOwner |
                    QFileDevice::ReadGroup |
                    QFileDevice::ReadOther)) {
            SourceLocation source;
            source.workbook = artifact.path;
            addDiagnostic(
                diagnostics,
                writeFailureCode,
                "Generated output was written, but could not be marked read-only: '" +
                    artifact.path.string() + "'.",
                {},
                std::move(source));
        }
    }
    return diagnostics;
}

} // namespace regmap
