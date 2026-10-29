#pragma once

#include "regmap/core/model.hpp"
#include "regmap/core/file_io.hpp"

#include <string>

namespace regmap {

enum class DiagnosticSeverity {
    information,
    warning,
    error,
};

struct Diagnostic {
    std::string code;
    DiagnosticSeverity severity {DiagnosticSeverity::error};
    std::string message;
    ObjectId objectId;
    SourceLocation source;
    std::optional<FileIoError> fileError{};
    bool operator==(const Diagnostic&) const = default;
};

} // namespace regmap
