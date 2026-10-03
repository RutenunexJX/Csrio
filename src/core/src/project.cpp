#include "regmap/core/project.hpp"

#include "regmap/core/serialization.hpp"
#include "regmap/core/validation.hpp"
#include "project_document.hpp"

#include <algorithm>
#include <iterator>

namespace regmap {

bool ProjectOpenResult::hasErrors() const noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

ProjectLoadSnapshot loadProjectSnapshot(const std::filesystem::path& manifestPath)
{
    ProjectLoadSnapshot result;
    auto document = detail::readProjectDocument(manifestPath, true);
    result.metrics_ = document.metrics;
    result.sourceSha256_ = std::move(document.sha256);
    if (!document.root) {
        result.loadDiagnostics_ = std::move(document.diagnostics);
        return result;
    }
    auto manifestResult = detail::decodeProjectManifest(*document.root, document.path);
    result.loadDiagnostics_ = std::move(manifestResult.diagnostics);
    if (!manifestResult.manifest.has_value()) {
        return result;
    }

    result.manifest_ = std::move(manifestResult.manifest);
    auto loadResult = detail::decodeProjectWorkspace(*document.root, document.path);
    document.root.reset(); // Decoded values own their data; release the YAML tree before validation.
    std::move(
        loadResult.diagnostics.begin(),
        loadResult.diagnostics.end(),
        std::back_inserter(result.loadDiagnostics_));

    if (!loadResult.workspace.has_value()) {
        return result;
    }

    ++result.metrics_.modelValidations;
    result.modelDiagnostics_ = validateWorkspace(*loadResult.workspace);
    result.workspace_ = std::move(loadResult.workspace);
    return result;
}

ProjectOpenResult openProject(const std::filesystem::path& manifestPath)
{
    auto loaded = loadProjectSnapshot(manifestPath);
    ProjectOpenResult result;
    result.manifest = std::move(loaded.manifest_);
    result.workspace = std::move(loaded.workspace_);
    result.diagnostics = std::move(loaded.loadDiagnostics_);
    std::move(
        loaded.modelDiagnostics_.begin(),
        loaded.modelDiagnostics_.end(),
        std::back_inserter(result.diagnostics));
    return result;
}

} // namespace regmap
