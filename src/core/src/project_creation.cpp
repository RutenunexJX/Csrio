#include "regmap/core/project_creation.hpp"

#include "regmap/core/workspace_store.hpp"

#include <cctype>
#include <string_view>
#include <utility>

namespace regmap {
namespace {

[[nodiscard]] std::string
systemVerilogIdentifier(
    std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 1);
    bool hasAsciiAlphanumeric =
        false;
    for (const char rawCharacter : value) {
        const auto character =
            static_cast<unsigned char>(
                rawCharacter);
        if (std::isalnum(character) != 0) {
            result.push_back(
                rawCharacter);
            hasAsciiAlphanumeric = true;
        } else if (
            result.empty() ||
            result.back() != '_') {
            result.push_back('_');
        }
    }
    while (!result.empty() &&
           result.back() == '_') {
        result.pop_back();
    }
    if (!hasAsciiAlphanumeric) {
        result = "register_map";
    }
    const auto first =
        static_cast<unsigned char>(
            result.front());
    if (std::isalpha(first) == 0 &&
        result.front() != '_') {
        result.insert(
            result.begin(), '_');
    }
    return result;
}

} // namespace

NewProject makeDefaultProject(
    const std::filesystem::path& manifestPath,
    std::string workspaceName,
    std::optional<ObjectId> workspaceId)
{
    NewProject result;
    const std::filesystem::path path =
        std::filesystem::absolute(
            manifestPath)
            .lexically_normal();

    result.workspace.name =
        std::move(workspaceName);
    result.workspace.manifestPath =
        path;
    if (workspaceId &&
        !workspaceId->empty()) {
        result.workspace.id =
            std::move(*workspaceId);
    } else {
        result.workspace.id =
            makeStableObjectId(
                result.workspace,
                "workspace");
    }

    result.manifest.manifestPath =
        path;
    result.manifest.workspaceId =
        result.workspace.id;
    result.manifest.workspaceName =
        result.workspace.name;
    const std::string baseName =
        systemVerilogIdentifier(
            result.workspace.name);
    result.manifest.rtl.moduleName =
        baseName + "_registers";
    result.manifest.rtl.path.declared =
        std::filesystem::path("rtl") /
        (result.manifest.rtl.moduleName +
         ".sv");
    result.manifest.rtl.path.resolved =
        (path.parent_path() /
         result.manifest.rtl.path.declared)
            .lexically_normal();
    result.manifest.outputDirectory
        .declared = "generated";
    result.manifest.outputDirectory
        .resolved =
        (path.parent_path() /
         result.manifest.outputDirectory
             .declared)
            .lexically_normal();

    const auto addTarget =
        [&](GenerationTargetKind kind,
            std::filesystem::path name) {
            GenerationTargetConfig target;
            target.kind = kind;
            target.path.declared =
                std::move(name);
            target.path.resolved =
                (result.manifest
                     .outputDirectory
                     .resolved /
                 target.path.declared)
                    .lexically_normal();
            result.manifest.targets
                .push_back(
                    std::move(target));
        };
    addTarget(
        GenerationTargetKind::xlsx,
        "register-map.xlsx");
    addTarget(
        GenerationTargetKind::cHeader,
        baseName + "_regs.h");
    addTarget(
        GenerationTargetKind::markdown,
        "register-map.md");
    return result;
}

} // namespace regmap
