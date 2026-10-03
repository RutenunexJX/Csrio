#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace regmap {

struct ProjectLoadMetrics {
    std::size_t fileReads{0};
    std::size_t yamlParses{0};
    std::size_t modelValidations{0};
};

struct ProjectOpenResult;
class WorkspaceStore;

// Only the loader can construct the workspace/validation pair. Const access
// prevents callers from changing the model while retaining its old diagnostics.
class ProjectLoadSnapshot {
public:
    ProjectLoadSnapshot() = default;
    ProjectLoadSnapshot(const ProjectLoadSnapshot&) = default;
    ProjectLoadSnapshot& operator=(const ProjectLoadSnapshot&) = default;
    ProjectLoadSnapshot(ProjectLoadSnapshot&& other) noexcept { *this = std::move(other); }
    ProjectLoadSnapshot& operator=(ProjectLoadSnapshot&& other) noexcept
    {
        if (this == &other) return *this;
        manifest_ = std::exchange(other.manifest_, std::nullopt);
        workspace_ = std::exchange(other.workspace_, std::nullopt);
        loadDiagnostics_ = std::move(other.loadDiagnostics_);
        modelDiagnostics_ = std::move(other.modelDiagnostics_);
        sourceSha256_ = std::move(other.sourceSha256_);
        metrics_ = other.metrics_;
        return *this;
    }
    [[nodiscard]] const std::optional<ProjectManifest>& manifest() const noexcept { return manifest_; }
    [[nodiscard]] const std::optional<Workspace>& workspace() const noexcept { return workspace_; }
    [[nodiscard]] const std::vector<Diagnostic>& loadDiagnostics() const noexcept { return loadDiagnostics_; }
    [[nodiscard]] const std::string& sourceSha256() const noexcept { return sourceSha256_; }
    [[nodiscard]] const ProjectLoadMetrics& metrics() const noexcept { return metrics_; }

private:
    std::optional<ProjectManifest> manifest_;
    std::optional<Workspace> workspace_;
    std::vector<Diagnostic> loadDiagnostics_;
    std::vector<Diagnostic> modelDiagnostics_;
    std::string sourceSha256_;
    ProjectLoadMetrics metrics_;

    friend ProjectLoadSnapshot loadProjectSnapshot(const std::filesystem::path&);
    friend ProjectOpenResult openProject(const std::filesystem::path&);
    friend class WorkspaceStore;
};

[[nodiscard]] ProjectLoadSnapshot loadProjectSnapshot(const std::filesystem::path& manifestPath);

struct ProjectOpenResult {
    std::optional<ProjectManifest> manifest;
    std::optional<Workspace> workspace;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

[[nodiscard]] ProjectOpenResult openProject(const std::filesystem::path& manifestPath);

} // namespace regmap
