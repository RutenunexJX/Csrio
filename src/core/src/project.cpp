#include "regmap/core/project.hpp"

#include "regmap/core/serialization.hpp"
#include "regmap/core/validation.hpp"

#include <algorithm>
#include <iterator>

namespace regmap {

bool ProjectOpenResult::hasErrors() const noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

ProjectOpenResult openProject(const std::filesystem::path& manifestPath)
{
    ProjectOpenResult result;
    auto manifestResult = loadProjectManifest(manifestPath);
    result.diagnostics = std::move(manifestResult.diagnostics);
    if (!manifestResult.manifest.has_value()) {
        return result;
    }

    result.manifest = std::move(manifestResult.manifest);
    auto loadResult = loadWorkspaceFromProjectFile(result.manifest->manifestPath);
    std::move(
        loadResult.diagnostics.begin(),
        loadResult.diagnostics.end(),
        std::back_inserter(result.diagnostics));

    if (!loadResult.workspace.has_value()) {
        return result;
    }

    auto validationDiagnostics = validateWorkspace(*loadResult.workspace);
    std::move(
        validationDiagnostics.begin(),
        validationDiagnostics.end(),
        std::back_inserter(result.diagnostics));
    result.workspace = std::move(loadResult.workspace);
    return result;
}

} // namespace regmap
