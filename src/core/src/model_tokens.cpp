#include "regmap/core/model_tokens.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace regmap {
namespace {

[[nodiscard]] std::string normalizedToken(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const char rawCharacter : text) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        if (std::isalnum(character) != 0) {
            result.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return result;
}

} // namespace

std::optional<AccessMode> parseAccessMode(std::string_view text) noexcept
{
    const std::string token = normalizedToken(text);
    if (token == "none" || token == "na") {
        return AccessMode::none;
    }
    if (token == "ro" || token == "readonly" || token == "read") {
        return AccessMode::readOnly;
    }
    if (token == "wo" || token == "writeonly") {
        return AccessMode::writeOnly;
    }
    if (token == "rw" || token == "readwrite") {
        return AccessMode::readWrite;
    }
    return std::nullopt;
}

std::optional<FieldType> parseFieldType(std::string_view text) noexcept
{
    const std::string token = normalizedToken(text);
    if (token == "bits" || token == "bit") {
        return FieldType::bits;
    }
    if (token == "bool" || token == "boolean") {
        return FieldType::boolean;
    }
    if (token == "unsigned" || token == "uint") {
        return FieldType::unsignedInteger;
    }
    if (token == "signed" || token == "int") {
        return FieldType::signedInteger;
    }
    if (token == "enum" || token == "enumeration") {
        return FieldType::enumeration;
    }
    if (token == "field" || token == "struct" || token == "structure"
        || token == "compound") {
        return FieldType::structure;
    }
    if (token == "reserved") {
        return FieldType::reserved;
    }
    return std::nullopt;
}

std::optional<ReadSideEffect> parseReadSideEffect(std::string_view text) noexcept
{
    const std::string token = normalizedToken(text);
    if (token == "none") {
        return ReadSideEffect::none;
    }
    if (token == "clear" || token == "rc") {
        return ReadSideEffect::clear;
    }
    if (token == "set" || token == "rs") {
        return ReadSideEffect::set;
    }
    return std::nullopt;
}

std::optional<WriteSideEffect> parseWriteSideEffect(std::string_view text) noexcept
{
    const std::string token = normalizedToken(text);
    if (token == "none") {
        return WriteSideEffect::none;
    }
    if (token == "write" || token == "normal") {
        return WriteSideEffect::write;
    }
    if (token == "w1c" || token == "onetoclear") {
        return WriteSideEffect::oneToClear;
    }
    if (token == "w1s" || token == "onetoset") {
        return WriteSideEffect::oneToSet;
    }
    if (token == "w0c" || token == "zerotoclear") {
        return WriteSideEffect::zeroToClear;
    }
    if (token == "w0s" || token == "zerotoset") {
        return WriteSideEffect::zeroToSet;
    }
    if (token == "toggle") {
        return WriteSideEffect::toggle;
    }
    return std::nullopt;
}

std::string_view toString(AccessMode value) noexcept
{
    switch (value) {
    case AccessMode::none:
        return "none";
    case AccessMode::readOnly:
        return "ro";
    case AccessMode::writeOnly:
        return "wo";
    case AccessMode::readWrite:
        return "rw";
    }
    return "none";
}

std::string_view toString(FieldType value) noexcept
{
    switch (value) {
    case FieldType::bits:
        return "bits";
    case FieldType::boolean:
        return "bool";
    case FieldType::unsignedInteger:
        return "unsigned";
    case FieldType::signedInteger:
        return "signed";
    case FieldType::enumeration:
        return "enum";
    case FieldType::structure:
        return "field";
    case FieldType::reserved:
        return "reserved";
    }
    return "bits";
}

std::string_view toString(ReadSideEffect value) noexcept
{
    switch (value) {
    case ReadSideEffect::none:
        return "none";
    case ReadSideEffect::clear:
        return "clear";
    case ReadSideEffect::set:
        return "set";
    }
    return "none";
}

std::string_view toString(WriteSideEffect value) noexcept
{
    switch (value) {
    case WriteSideEffect::none:
        return "none";
    case WriteSideEffect::write:
        return "write";
    case WriteSideEffect::oneToClear:
        return "w1c";
    case WriteSideEffect::oneToSet:
        return "w1s";
    case WriteSideEffect::zeroToClear:
        return "w0c";
    case WriteSideEffect::zeroToSet:
        return "w0s";
    case WriteSideEffect::toggle:
        return "toggle";
    }
    return "none";
}

} // namespace regmap
