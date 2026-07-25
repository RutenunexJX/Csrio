#include "regmap/core/manifest.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string_view>

namespace regmap {
namespace {

constexpr std::string_view invalidYamlCode = "RM1000";
constexpr std::string_view missingValueCode = "RM1001";
constexpr std::string_view invalidValueCode = "RM1002";
constexpr std::string_view unsupportedVersionCode = "RM1003";
constexpr std::string_view duplicateIdCode = "RM1004";

[[nodiscard]] SourceLocation yamlLocation(
    const std::filesystem::path& path,
    const YAML::Mark& mark,
    std::string yamlPath = {})
{
    SourceLocation location;
    location.workbook = path;
    if (!mark.is_null()) {
        location.row = static_cast<std::uint32_t>(mark.line + 1);
        location.column = static_cast<std::uint32_t>(mark.column + 1);
    }
    location.cell = std::move(yamlPath);
    return location;
}

void addDiagnostic(
    std::vector<Diagnostic>& diagnostics,
    std::string_view code,
    std::string message,
    const std::filesystem::path& path,
    const YAML::Node& node = {},
    std::string yamlPath = {})
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.source = yamlLocation(path, node.Mark(), std::move(yamlPath));
    diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] std::optional<std::string> scalar(
    const YAML::Node& parent,
    std::string_view key,
    bool required,
    const std::filesystem::path& manifestPath,
    std::string_view yamlPath,
    std::vector<Diagnostic>& diagnostics)
{
    const YAML::Node node = parent[std::string(key)];
    const std::string fullPath = std::string(yamlPath) + '.' + std::string(key);
    if (!node) {
        if (required) {
            addDiagnostic(
                diagnostics,
                missingValueCode,
                "Missing required value '" + fullPath + "'.",
                manifestPath,
                parent,
                fullPath);
        }
        return std::nullopt;
    }
    if (!node.IsScalar()) {
        addDiagnostic(
            diagnostics,
            invalidValueCode,
            "Value '" + fullPath + "' must be a scalar.",
            manifestPath,
            node,
            fullPath);
        return std::nullopt;
    }

    const std::string value = node.Scalar();
    if (value.empty()) {
        addDiagnostic(
            diagnostics,
            invalidValueCode,
            "Value '" + fullPath + "' must not be empty.",
            manifestPath,
            node,
            fullPath);
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool hasWhitespace(std::string_view value) noexcept
{
    return std::ranges::any_of(value, [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool isValidId(std::string_view value) noexcept
{
    return !value.empty() && !hasWhitespace(value);
}

[[nodiscard]] bool isValidSystemVerilogIdentifier(std::string_view value) noexcept
{
    if (value.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(value.front());
    if (std::isalpha(first) == 0 && value.front() != '_') {
        return false;
    }
    return std::ranges::all_of(value.substr(1), [](char rawCharacter) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        return std::isalnum(character) != 0 || rawCharacter == '_' || rawCharacter == '$';
    });
}

[[nodiscard]] std::optional<ManifestPath> manifestRelativePath(
    std::string_view value,
    const std::filesystem::path& manifestPath,
    const YAML::Node& node,
    std::string_view yamlPath,
    std::vector<Diagnostic>& diagnostics)
{
    const std::filesystem::path declared = std::filesystem::path(value);
    if (declared.empty() || declared.is_absolute() || declared.has_root_name()) {
        addDiagnostic(
            diagnostics,
            invalidValueCode,
            "Path '" + std::string(yamlPath) + "' must be relative to the manifest.",
            manifestPath,
            node,
            std::string(yamlPath));
        return std::nullopt;
    }

    ManifestPath result;
    result.declared = declared.lexically_normal();
    if (!result.declared.empty() && *result.declared.begin() == "..") {
        addDiagnostic(
            diagnostics,
            invalidValueCode,
            "Path '" + std::string(yamlPath) + "' must stay inside the project directory.",
            manifestPath,
            node,
            std::string(yamlPath));
        return std::nullopt;
    }
    result.resolved = (manifestPath.parent_path() / declared).lexically_normal();
    return result;
}

[[nodiscard]] std::optional<GenerationTargetKind> targetKind(std::string_view value) noexcept
{
    if (value == "xlsx") {
        return GenerationTargetKind::xlsx;
    }
    if (value == "c-header") {
        return GenerationTargetKind::cHeader;
    }
    if (value == "markdown") {
        return GenerationTargetKind::markdown;
    }
    return std::nullopt;
}

void parseWorkspace(
    const YAML::Node& root,
    ProjectManifest& manifest,
    std::vector<Diagnostic>& diagnostics)
{
    const YAML::Node workspace = root["workspace"];
    if (!workspace || !workspace.IsMap()) {
        addDiagnostic(
            diagnostics,
            missingValueCode,
            "Required mapping 'workspace' is missing.",
            manifest.manifestPath,
            root,
            "workspace");
        return;
    }

    const auto id = scalar(
        workspace, "id", true, manifest.manifestPath, "workspace", diagnostics);
    if (id) {
        if (isValidId(*id)) {
            manifest.workspaceId = *id;
        } else {
            addDiagnostic(
                diagnostics,
                invalidValueCode,
                "Value 'workspace.id' must be a non-empty stable ID without whitespace.",
                manifest.manifestPath,
                workspace["id"],
                "workspace.id");
        }
    }

    const auto name = scalar(
        workspace, "name", true, manifest.manifestPath, "workspace", diagnostics);
    if (name) {
        manifest.workspaceName = *name;
    }
}

void parseRtl(
    const YAML::Node& root,
    ProjectManifest& manifest,
    std::vector<Diagnostic>& diagnostics)
{
    const YAML::Node rtl = root["rtl"];
    if (!rtl || !rtl.IsMap()) {
        addDiagnostic(
            diagnostics,
            missingValueCode,
            "Required mapping 'rtl' is missing.",
            manifest.manifestPath,
            rtl ? rtl : root,
            "rtl");
        return;
    }

    const auto path = scalar(rtl, "path", true, manifest.manifestPath, "rtl", diagnostics);
    if (path) {
        const auto parsedPath = manifestRelativePath(
            *path, manifest.manifestPath, rtl["path"], "rtl.path", diagnostics);
        if (parsedPath) {
            manifest.rtl.path = *parsedPath;
        }
    }

    const auto moduleName = scalar(
        rtl, "module", true, manifest.manifestPath, "rtl", diagnostics);
    if (moduleName) {
        if (isValidSystemVerilogIdentifier(*moduleName)) {
            manifest.rtl.moduleName = *moduleName;
        } else {
            addDiagnostic(
                diagnostics,
                invalidValueCode,
                "Value 'rtl.module' must be a valid SystemVerilog identifier.",
                manifest.manifestPath,
                rtl["module"],
                "rtl.module");
        }
    }
}

void parseTargetOptions(
    const YAML::Node& options,
    GenerationTargetConfig& config,
    const ProjectManifest& manifest,
    std::string_view targetPath,
    std::vector<Diagnostic>& diagnostics)
{
    if (!options) {
        return;
    }
    if (!options.IsMap()) {
        addDiagnostic(
            diagnostics,
            invalidValueCode,
            "Value '" + std::string(targetPath) + ".options' must be a mapping.",
            manifest.manifestPath,
            options,
            std::string(targetPath) + ".options");
        return;
    }

    for (const auto& entry : options) {
        if (!entry.first.IsScalar() || !entry.second.IsScalar()) {
            addDiagnostic(
                diagnostics,
                invalidValueCode,
                "Generation option names and values must be scalars.",
                manifest.manifestPath,
                entry.second,
                std::string(targetPath) + ".options");
            continue;
        }
        if (entry.second.Scalar().empty()) {
            addDiagnostic(
                diagnostics,
                invalidValueCode,
                "Generation option values must not be empty.",
                manifest.manifestPath,
                entry.second,
                std::string(targetPath) + ".options." + entry.first.Scalar());
            continue;
        }
        config.options.insert_or_assign(entry.first.Scalar(), entry.second.Scalar());
    }
}

void parseGeneration(
    const YAML::Node& root,
    ProjectManifest& manifest,
    std::vector<Diagnostic>& diagnostics)
{
    const YAML::Node generation = root["generation"];
    if (!generation || !generation.IsMap()) {
        addDiagnostic(
            diagnostics,
            missingValueCode,
            "Required mapping 'generation' is missing.",
            manifest.manifestPath,
            root,
            "generation");
        return;
    }

    const auto outputDirectory = scalar(
        generation,
        "output_directory",
        false,
        manifest.manifestPath,
        "generation",
        diagnostics);
    const std::string outputDirectoryValue = outputDirectory.value_or("generated");
    const YAML::Node outputDirectoryNode = generation["output_directory"];
    if (const auto parsedPath = manifestRelativePath(
            outputDirectoryValue,
            manifest.manifestPath,
            outputDirectoryNode ? outputDirectoryNode : generation,
            "generation.output_directory",
            diagnostics)) {
        manifest.outputDirectory = *parsedPath;
    }

    const YAML::Node targets = generation["targets"];
    if (!targets || !targets.IsSequence() || targets.size() == 0) {
        addDiagnostic(
            diagnostics,
            missingValueCode,
            "'generation.targets' must contain at least one target.",
            manifest.manifestPath,
            targets ? targets : generation,
            "generation.targets");
        return;
    }

    for (std::size_t index = 0; index < targets.size(); ++index) {
        const YAML::Node target = targets[index];
        const std::string targetPathValue = "generation.targets[" + std::to_string(index) + ']';
        if (!target.IsMap()) {
            addDiagnostic(
                diagnostics,
                invalidValueCode,
                "Value '" + targetPathValue + "' must be a mapping.",
                manifest.manifestPath,
                target,
                targetPathValue);
            continue;
        }

        GenerationTargetConfig config;
        std::optional<GenerationTargetKind> parsedTargetKind;
        const auto kind = scalar(
            target, "kind", true, manifest.manifestPath, targetPathValue, diagnostics);
        if (kind) {
            parsedTargetKind = targetKind(*kind);
            if (parsedTargetKind) {
                config.kind = *parsedTargetKind;
            } else {
                addDiagnostic(
                    diagnostics,
                    invalidValueCode,
                    "Unknown generation target kind '" + *kind + "'.",
                    manifest.manifestPath,
                    target["kind"],
                    targetPathValue + ".kind");
            }
        }

        const auto path = scalar(
            target, "path", true, manifest.manifestPath, targetPathValue, diagnostics);
        if (path) {
            const std::filesystem::path declared = std::filesystem::path(*path);
            const std::filesystem::path normalized = declared.lexically_normal();
            const bool escapesOutputDirectory = !normalized.empty()
                && *normalized.begin() == "..";
            if (declared.empty() || normalized == "." || declared.is_absolute()
                || declared.has_root_name() || escapesOutputDirectory) {
                addDiagnostic(
                    diagnostics,
                    invalidValueCode,
                    "Path '" + targetPathValue
                        + ".path' must name a file inside the output directory.",
                    manifest.manifestPath,
                    target["path"],
                    targetPathValue + ".path");
            } else {
                config.path.declared = normalized;
                config.path.resolved = (manifest.outputDirectory.resolved / normalized)
                                           .lexically_normal();
            }
        }

        parseTargetOptions(
            target["options"], config, manifest, targetPathValue, diagnostics);
        if (parsedTargetKind) {
            for (const auto& [name, value] : config.options) {
                static_cast<void>(value);
                const bool supported =
                    (*parsedTargetKind == GenerationTargetKind::cHeader && name == "guard")
                    || (*parsedTargetKind == GenerationTargetKind::markdown && name == "title");
                if (!supported) {
                    addDiagnostic(
                        diagnostics,
                        invalidValueCode,
                        "Unsupported option '" + name + "' for target kind '"
                            + std::string(toString(*parsedTargetKind)) + "'.",
                        manifest.manifestPath,
                        target["options"][name],
                        targetPathValue + ".options." + name);
                }
            }
        }
        manifest.targets.push_back(std::move(config));
    }
}

void validateManifestContract(
    const ProjectManifest& manifest,
    std::vector<Diagnostic>& diagnostics)
{
    std::set<std::filesystem::path> paths;
    std::set<GenerationTargetKind> kinds;
    for (const auto& target : manifest.targets) {
        if (!target.path.resolved.empty() && !paths.insert(target.path.resolved).second) {
            addDiagnostic(
                diagnostics,
                duplicateIdCode,
                "Generation target paths must be unique.",
                manifest.manifestPath,
                {},
                "generation.targets");
        }
        if (!kinds.insert(target.kind).second) {
            addDiagnostic(
                diagnostics,
                duplicateIdCode,
                "Each generation target kind may be declared only once.",
                manifest.manifestPath,
                {},
                "generation.targets");
        }
        if (target.path.resolved == manifest.manifestPath
            || target.path.resolved == manifest.rtl.path.resolved) {
            addDiagnostic(
                diagnostics,
                invalidValueCode,
                "A generated output must not overwrite the project or managed RTL file.",
                manifest.manifestPath,
                {},
                "generation.targets");
        }
    }

    for (const auto required : {
             GenerationTargetKind::xlsx,
             GenerationTargetKind::cHeader,
             GenerationTargetKind::markdown}) {
        if (!kinds.contains(required)) {
            addDiagnostic(
                diagnostics,
                missingValueCode,
                "Generation targets must include xlsx, c-header, and markdown exactly once.",
                manifest.manifestPath,
                {},
                "generation.targets");
            break;
        }
    }
}

} // namespace

bool ManifestLoadResult::hasErrors() const noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

ManifestLoadResult loadProjectManifest(const std::filesystem::path& path)
{
    ManifestLoadResult result;
    ProjectManifest manifest;
    manifest.manifestPath = std::filesystem::absolute(path).lexically_normal();

    YAML::Node root;
    try {
        root = YAML::LoadFile(manifest.manifestPath.string());
    } catch (const YAML::Exception& error) {
        Diagnostic diagnostic;
        diagnostic.code = invalidYamlCode;
        diagnostic.message = "Cannot parse manifest: " + std::string(error.what());
        diagnostic.source = yamlLocation(manifest.manifestPath, error.mark);
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    if (!root.IsMap()) {
        addDiagnostic(
            result.diagnostics,
            invalidYamlCode,
            "Manifest root must be a mapping.",
            manifest.manifestPath,
            root);
        return result;
    }

    const YAML::Node schemaVersion = root["schema_version"];
    if (!schemaVersion || !schemaVersion.IsScalar()) {
        addDiagnostic(
            result.diagnostics,
            missingValueCode,
            "Required scalar 'schema_version' is missing.",
            manifest.manifestPath,
            root,
            "schema_version");
    } else {
        try {
            manifest.schemaVersion = schemaVersion.as<int>();
            if (manifest.schemaVersion != ProjectManifest::currentSchemaVersion) {
                addDiagnostic(
                    result.diagnostics,
                    unsupportedVersionCode,
                    "Unsupported schema version " + std::to_string(manifest.schemaVersion)
                        + "; expected "
                        + std::to_string(ProjectManifest::currentSchemaVersion) + '.',
                    manifest.manifestPath,
                    schemaVersion,
                    "schema_version");
            }
        } catch (const YAML::Exception&) {
            addDiagnostic(
                result.diagnostics,
                invalidValueCode,
                "Value 'schema_version' must be an integer.",
                manifest.manifestPath,
                schemaVersion,
                "schema_version");
        }
    }

    parseWorkspace(root, manifest, result.diagnostics);
    parseRtl(root, manifest, result.diagnostics);
    parseGeneration(root, manifest, result.diagnostics);
    validateManifestContract(manifest, result.diagnostics);

    if (!result.hasErrors()) {
        result.manifest = std::move(manifest);
    }
    return result;
}

std::string_view toString(GenerationTargetKind kind) noexcept
{
    switch (kind) {
    case GenerationTargetKind::xlsx:
        return "xlsx";
    case GenerationTargetKind::cHeader:
        return "c-header";
    case GenerationTargetKind::markdown:
        return "markdown";
    }
    return "unknown";
}

} // namespace regmap
