#pragma once

#include "regmap/core/model.hpp"

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
    bool operator==(const Diagnostic&) const = default;
};

} // namespace regmap
