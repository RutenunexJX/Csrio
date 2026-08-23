#include "cli_app.hpp"
#include "cli_artifacts.hpp"

#include "regmap/core/generation.hpp"
#include "regmap/core/model_tokens.hpp"
#include "regmap/core/project.hpp"
#include "regmap/core/project_creation.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/sync_diff.hpp"
#include "regmap/core/validation.hpp"
#include "regmap/core/workspace_store.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace regmap::cli {
namespace {

constexpr std::string_view objectNotFoundCode =
    "RMC2001";
constexpr std::string_view ambiguousFindMatchCode =
    "RMC2002";
constexpr std::string_view revisionConflictCode =
    "RMC3001";
constexpr std::string_view revisionReadFailureCode =
    "RMC4001";
constexpr std::string_view projectVerificationFailureCode =
    "RMC4002";
constexpr std::string_view outputsOutOfDateCode =
    "RMC5001";
constexpr std::string_view differencesFoundCode =
    "RMC6001";
constexpr std::string_view genericUsageFailureCode =
    "RMC1000";
constexpr std::string_view genericProjectFailureCode =
    "RMC2000";
constexpr std::string_view genericRevisionFailureCode =
    "RMC3000";
constexpr std::string_view genericWriteFailureCode =
    "RMC4000";
constexpr std::string_view genericOutputsOutOfDateCode =
    "RMC5000";
constexpr std::string_view genericDifferencesFoundCode =
    "RMC6000";
constexpr std::string_view genericInputFailureCode =
    "RMC7000";
constexpr std::string_view genericGenerationFailureCode =
    "RMC8000";
constexpr std::string_view requestedTargetUnavailableCode =
    "RMC2003";
constexpr qsizetype defaultQueryLimit = 100;
constexpr double largestExactJsonInteger =
    9007199254740991.0;

struct CommandResponse {
    QString command;
    bool ok{false};
    QString project;
    QString revision;
    QString usage;
    QJsonValue result;
    QJsonObject resultMetadata;
    std::vector<Diagnostic> diagnostics;
    QString error;
    ExitCode exitCode{ExitCode::success};
};

struct OpenedProject {
    ProjectOpenResult open;
    QString path;
    QString revision;
};

struct ObjectDescriptor {
    QString kind;
    QString id;
    QString name;
    QString parentId;
    QString path;
    QJsonObject extra;
};

struct SearchMatch {
    int rank{0};
    QString field;
};

struct ApplyOptions {
    QString projectPath;
    QString patchPath;
    QString expectedRevision;
    bool dryRun{false};
    bool force{false};
};

struct GenerateOptions {
    QString projectPath;
    QString expectedRevision;
    GenerationTargetSet targets;
    bool dryRun{false};
};

struct InitOptions {
    QString projectPath;
    QString name;
    QString workspaceId;
    QString outputDirectory;
    GenerationTargetSet targets;
    std::map<GenerationTargetKind, QString>
        targetFileNames;
    bool generate{true};
};

struct StatusOptions {
    QString projectPath;
    GenerationTargetSet targets;
    bool requireCurrent{false};
};

struct DiffOptions {
    QString beforeProjectPath;
    QString afterProjectPath;
    QString kind{
        QStringLiteral("all")};
    QString expectedBeforeRevision;
    QString expectedAfterRevision;
    std::optional<qsizetype> limit;
    qsizetype offset{0};
    bool requireEqual{false};
};

[[nodiscard]] bool hasOnlyKeys(
    const QJsonObject& object,
    const std::set<QString>& allowed,
    QString& error);

[[nodiscard]] QString fromUtf8(
    std::string_view value)
{
    return QString::fromUtf8(
        value.data(),
        static_cast<qsizetype>(
            value.size()));
}

[[nodiscard]] QString fromPath(
    const std::filesystem::path& path)
{
    return QString::fromStdWString(
        path.wstring());
}

[[nodiscard]] std::filesystem::path toPath(
    const QString& path)
{
    return std::filesystem::path(
        path.toStdWString());
}

[[nodiscard]] QString hex(
    std::uint64_t value)
{
    return QStringLiteral("0x%1")
        .arg(
            QString::number(
                value, 16)
                .toUpper());
}

[[nodiscard]] QJsonValue optionalText(
    const std::optional<std::string>& value)
{
    return value
        ? QJsonValue(fromUtf8(*value))
        : QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QJsonValue optionalValue(
    const std::optional<UnsignedValue>& value)
{
    return value
        ? QJsonValue(
              fromUtf8(
                  value->toHexString()))
        : QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QString severityText(
    DiagnosticSeverity severity)
{
    switch (severity) {
    case DiagnosticSeverity::information:
        return QStringLiteral(
            "information");
    case DiagnosticSeverity::warning:
        return QStringLiteral("warning");
    case DiagnosticSeverity::error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

[[nodiscard]] QString exitStatus(
    ExitCode exitCode)
{
    switch (exitCode) {
    case ExitCode::success:
        return QStringLiteral("success");
    case ExitCode::projectError:
        return QStringLiteral("project_error");
    case ExitCode::usageError:
        return QStringLiteral("usage_error");
    case ExitCode::revisionConflict:
        return QStringLiteral("revision_conflict");
    case ExitCode::writeError:
        return QStringLiteral("write_error");
    case ExitCode::outputsOutOfDate:
        return QStringLiteral(
            "outputs_out_of_date");
    case ExitCode::differencesFound:
        return QStringLiteral(
            "differences_found");
    case ExitCode::inputError:
        return QStringLiteral("input_error");
    case ExitCode::generationError:
        return QStringLiteral(
            "generation_error");
    }
    return QStringLiteral("usage_error");
}

[[nodiscard]] QString primaryErrorCode(
    const CommandResponse& response)
{
    if (response.exitCode ==
        ExitCode::success) {
        return {};
    }
    const auto diagnostic =
        std::ranges::find_if(
            response.diagnostics,
            [](const Diagnostic& candidate) {
                return candidate.severity ==
                        DiagnosticSeverity::error &&
                    !candidate.code.empty();
            });
    if (diagnostic !=
        response.diagnostics.end()) {
        return fromUtf8(
            diagnostic->code);
    }
    switch (response.exitCode) {
    case ExitCode::success:
        return {};
    case ExitCode::usageError:
        return fromUtf8(
            genericUsageFailureCode);
    case ExitCode::projectError:
        return fromUtf8(
            genericProjectFailureCode);
    case ExitCode::revisionConflict:
        return fromUtf8(
            genericRevisionFailureCode);
    case ExitCode::writeError:
        return fromUtf8(
            genericWriteFailureCode);
    case ExitCode::outputsOutOfDate:
        return fromUtf8(
            genericOutputsOutOfDateCode);
    case ExitCode::differencesFound:
        return fromUtf8(
            genericDifferencesFoundCode);
    case ExitCode::inputError:
        return fromUtf8(
            genericInputFailureCode);
    case ExitCode::generationError:
        return fromUtf8(
            genericGenerationFailureCode);
    }
    return fromUtf8(
        genericUsageFailureCode);
}

[[nodiscard]] bool hasErrors(
    const std::vector<Diagnostic>& diagnostics)
{
    return std::ranges::any_of(
        diagnostics,
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity ==
                DiagnosticSeverity::error;
        });
}

[[nodiscard]] QJsonObject sourceJson(
    const SourceLocation& source)
{
    QJsonObject result{
        {QStringLiteral("file"),
         fromPath(source.workbook)},
        {QStringLiteral("sheet"),
         fromUtf8(source.sheet)},
        {QStringLiteral("cell"),
         fromUtf8(source.cell)},
    };
    result.insert(
        QStringLiteral("row"),
        source.row
            ? QJsonValue(
                  static_cast<qint64>(
                      *source.row))
            : QJsonValue(
                  QJsonValue::Null));
    result.insert(
        QStringLiteral("column"),
        source.column
            ? QJsonValue(
                  static_cast<qint64>(
                      *source.column))
            : QJsonValue(
                  QJsonValue::Null));
    return result;
}

[[nodiscard]] QString objectKindToken(
    ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::workspace:
        return QStringLiteral("workspace");
    case ObjectKind::addressSpace:
        return QStringLiteral("page");
    case ObjectKind::registerBlock:
        return QStringLiteral("block");
    case ObjectKind::reg:
        return QStringLiteral("register");
    case ObjectKind::field:
        return QStringLiteral("field");
    case ObjectKind::enumValue:
        return QStringLiteral("enum");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString changeKindToken(
    ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::added:
        return QStringLiteral("added");
    case ChangeKind::removed:
        return QStringLiteral("removed");
    case ChangeKind::modified:
        return QStringLiteral("modified");
    }
    return QStringLiteral("unknown");
}

using DiffObjectStates =
    std::map<
        ObjectId,
        QJsonObject,
        std::less<>>;

[[nodiscard]] QJsonObject diffObjectState(
    QString kind,
    std::string_view id,
    std::string_view parentId,
    std::size_t order,
    QJsonObject properties)
{
    return {
        {QStringLiteral("kind"),
         std::move(kind)},
        {QStringLiteral("id"),
         fromUtf8(id)},
        {QStringLiteral("parent_id"),
         parentId.empty()
             ? QJsonValue(
                   QJsonValue::Null)
             : QJsonValue(
                   fromUtf8(
                       parentId))},
        {QStringLiteral("order"),
         static_cast<qint64>(
             order)},
        {QStringLiteral("properties"),
         std::move(properties)},
    };
}

void collectEnumDiffStates(
    DiffObjectStates& states,
    const std::vector<EnumValue>& values,
    std::string_view parentId)
{
    for (std::size_t index = 0;
         index < values.size();
         ++index) {
        const EnumValue& value =
            values.at(index);
        states.insert_or_assign(
            value.id,
            diffObjectState(
                QStringLiteral("enum"),
                value.id,
                parentId,
                index,
                QJsonObject{
                    {QStringLiteral("name"),
                     fromUtf8(value.name)},
                    {QStringLiteral("value"),
                     fromUtf8(
                         value.value
                             .toHexString())},
                    {QStringLiteral(
                         "description"),
                     fromUtf8(
                         value.description)},
                }));
    }
}

void collectFieldDiffStates(
    DiffObjectStates& states,
    const std::vector<Field>& fields,
    std::string_view parentId)
{
    for (std::size_t index = 0;
         index < fields.size();
         ++index) {
        const Field& field =
            fields.at(index);
        states.insert_or_assign(
            field.id,
            diffObjectState(
                QStringLiteral("field"),
                field.id,
                parentId,
                index,
                QJsonObject{
                    {QStringLiteral("name"),
                     fromUtf8(field.name)},
                    {QStringLiteral("msb"),
                     static_cast<qint64>(
                         field.msb)},
                    {QStringLiteral("lsb"),
                     static_cast<qint64>(
                         field.lsb)},
                    {QStringLiteral("width"),
                     static_cast<qint64>(
                         field.width())},
                    {QStringLiteral("type"),
                     fromUtf8(
                         toString(
                             field.type))},
                    {QStringLiteral(
                         "software_access"),
                     fromUtf8(
                         toString(
                             field
                                 .softwareAccess))},
                    {QStringLiteral(
                         "hardware_access"),
                     fromUtf8(
                         toString(
                             field
                                 .hardwareAccess))},
                    {QStringLiteral("reset"),
                     optionalValue(
                         field.resetValue)},
                    {QStringLiteral(
                         "read_side_effect"),
                     fromUtf8(
                         toString(
                             field
                                 .readSideEffect))},
                    {QStringLiteral(
                         "write_side_effect"),
                     fromUtf8(
                         toString(
                             field
                                 .writeSideEffect))},
                    {QStringLiteral("minimum"),
                     optionalText(
                         field.minimumValue)},
                    {QStringLiteral("maximum"),
                     optionalText(
                         field.maximumValue)},
                    {QStringLiteral(
                         "description"),
                     fromUtf8(
                         field.description)},
                }));
        collectEnumDiffStates(
            states,
            field.enumValues,
            field.id);
        collectFieldDiffStates(
            states,
            field.members,
            field.id);
    }
}

[[nodiscard]] DiffObjectStates
collectDiffObjectStates(
    const Workspace& workspace)
{
    DiffObjectStates states;
    states.insert_or_assign(
        workspace.id,
        diffObjectState(
            QStringLiteral("workspace"),
            workspace.id,
            {},
            0,
            QJsonObject{
                {QStringLiteral("name"),
                 fromUtf8(
                     workspace.name)},
            }));
    for (std::size_t pageIndex = 0;
         pageIndex <
         workspace.addressSpaces.size();
         ++pageIndex) {
        const AddressSpace& page =
            workspace.addressSpaces.at(
                pageIndex);
        states.insert_or_assign(
            page.id,
            diffObjectState(
                QStringLiteral("page"),
                page.id,
                workspace.id,
                pageIndex,
                QJsonObject{
                    {QStringLiteral("name"),
                     fromUtf8(page.name)},
                    {QStringLiteral("base"),
                     hex(
                         page.baseAddress)},
                    {QStringLiteral(
                         "address_width"),
                     static_cast<qint64>(
                         page.addressWidth)},
                    {QStringLiteral(
                         "description"),
                     fromUtf8(
                         page.description)},
                }));
        for (std::size_t blockIndex = 0;
             blockIndex <
             page.blocks.size();
             ++blockIndex) {
            const RegisterBlock& block =
                page.blocks.at(
                    blockIndex);
            states.insert_or_assign(
                block.id,
                diffObjectState(
                    QStringLiteral("block"),
                    block.id,
                    page.id,
                    blockIndex,
                    QJsonObject{
                        {QStringLiteral("name"),
                         fromUtf8(
                             block.name)},
                        {QStringLiteral("base"),
                         hex(
                             block
                                 .baseAddress)},
                        {QStringLiteral("size"),
                         block.size
                             ? QJsonValue(
                                   hex(
                                       *block
                                            .size))
                             : QJsonValue(
                                   QJsonValue::
                                       Null)},
                        {QStringLiteral(
                             "description"),
                         fromUtf8(
                             block.description)},
                    }));
            for (std::size_t
                     registerIndex = 0;
                 registerIndex <
                 block.registers.size();
                 ++registerIndex) {
                const Register& reg =
                    block.registers.at(
                        registerIndex);
                QJsonArray tags;
                for (const std::string& tag :
                     reg.tags) {
                    tags.append(
                        fromUtf8(tag));
                }
                states.insert_or_assign(
                    reg.id,
                    diffObjectState(
                        QStringLiteral(
                            "register"),
                        reg.id,
                        block.id,
                        registerIndex,
                        QJsonObject{
                            {QStringLiteral(
                                 "name"),
                             fromUtf8(
                                 reg.name)},
                            {QStringLiteral(
                                 "offset"),
                             hex(reg.offset)},
                            {QStringLiteral(
                                 "fixed"),
                             reg.addressFixed},
                            {QStringLiteral(
                                 "width"),
                             static_cast<
                                 qint64>(
                                 reg.width)},
                            {QStringLiteral(
                                 "type"),
                             fromUtf8(
                                 toString(
                                     reg.type))},
                            {QStringLiteral(
                                 "minimum"),
                             optionalText(
                                 reg
                                     .minimumValue)},
                            {QStringLiteral(
                                 "maximum"),
                             optionalText(
                                 reg
                                     .maximumValue)},
                            {QStringLiteral(
                                 "initial"),
                             optionalValue(
                                 reg
                                     .initialValue)},
                            {QStringLiteral(
                                 "reset"),
                             optionalValue(
                                 reg
                                     .resetValue)},
                            {QStringLiteral(
                                 "access"),
                             fromUtf8(
                                 toString(
                                     reg.access))},
                            {QStringLiteral(
                                 "reserved"),
                             reg.reserved},
                            {QStringLiteral(
                                 "tags"),
                             tags},
                            {QStringLiteral(
                                 "description"),
                             fromUtf8(
                                 reg.description)},
                        }));
                collectEnumDiffStates(
                    states,
                    reg.enumValues,
                    reg.id);
                collectFieldDiffStates(
                    states,
                    reg.fields,
                    reg.id);
            }
        }
    }
    return states;
}

[[nodiscard]] QJsonObject comparableState(
    const QJsonObject& state)
{
    QJsonObject result =
        state.value(
                 QStringLiteral(
                     "properties"))
            .toObject();
    for (const QString& member :
         {QStringLiteral("kind"),
          QStringLiteral("parent_id"),
          QStringLiteral("order")}) {
        result.insert(
            member,
            state.value(member));
    }
    return result;
}

[[nodiscard]] QJsonArray propertyChangesJson(
    const QJsonObject& before,
    const QJsonObject& after)
{
    const QJsonObject beforeValues =
        comparableState(before);
    const QJsonObject afterValues =
        comparableState(after);
    std::set<QString> properties;
    for (auto iterator =
             beforeValues.constBegin();
         iterator !=
         beforeValues.constEnd();
         ++iterator) {
        properties.insert(
            iterator.key());
    }
    for (auto iterator =
             afterValues.constBegin();
         iterator !=
         afterValues.constEnd();
         ++iterator) {
        properties.insert(
            iterator.key());
    }
    QJsonArray result;
    for (const QString& property :
         properties) {
        const bool beforePresent =
            beforeValues.contains(
                property);
        const bool afterPresent =
            afterValues.contains(
                property);
        const QJsonValue beforeValue =
            beforeValues.value(
                property);
        const QJsonValue afterValue =
            afterValues.value(
                property);
        if (beforePresent ==
                afterPresent &&
            beforeValue ==
                afterValue) {
            continue;
        }
        result.append(
            QJsonObject{
                {QStringLiteral(
                     "property"),
                 property},
                {QStringLiteral(
                     "before_present"),
                 beforePresent},
                {QStringLiteral(
                     "before"),
                 beforePresent
                     ? beforeValue
                     : QJsonValue(
                           QJsonValue::
                               Null)},
                {QStringLiteral(
                     "after_present"),
                 afterPresent},
                {QStringLiteral(
                     "after"),
                 afterPresent
                     ? afterValue
                     : QJsonValue(
                           QJsonValue::
                               Null)},
            });
    }
    return result;
}

[[nodiscard]] QJsonObject modelChangeJson(
    const ModelChange& change,
    const QJsonObject* beforeState,
    const QJsonObject* afterState)
{
    QJsonObject result{
        {QStringLiteral("change"),
         changeKindToken(change.change)},
        {QStringLiteral("object_kind"),
         objectKindToken(
             change.objectKind)},
        {QStringLiteral("id"),
         fromUtf8(change.id)},
        {QStringLiteral("name"),
         fromUtf8(change.name)},
        {QStringLiteral("summary"),
         fromUtf8(change.summary)},
    };
    result.insert(
        QStringLiteral("before_source"),
        change.change == ChangeKind::added
            ? QJsonValue(
                  QJsonValue::Null)
            : QJsonValue(
                  sourceJson(
                      change.beforeSource)));
    result.insert(
        QStringLiteral("after_source"),
        change.change == ChangeKind::removed
            ? QJsonValue(
                  QJsonValue::Null)
            : QJsonValue(
                  sourceJson(
                      change.afterSource)));
    result.insert(
        QStringLiteral("before"),
        beforeState
            ? QJsonValue(
                  *beforeState)
            : QJsonValue(
                  QJsonValue::Null));
    result.insert(
        QStringLiteral("after"),
        afterState
            ? QJsonValue(
                  *afterState)
            : QJsonValue(
                  QJsonValue::Null));
    result.insert(
        QStringLiteral(
            "property_changes"),
        beforeState && afterState
            ? QJsonValue(
                  propertyChangesJson(
                      *beforeState,
                      *afterState))
            : QJsonValue(
                  QJsonArray{}));
    return result;
}

[[nodiscard]] QJsonObject diagnosticJson(
    const Diagnostic& diagnostic)
{
    return {
        {QStringLiteral("severity"),
         severityText(
             diagnostic.severity)},
        {QStringLiteral("code"),
         fromUtf8(diagnostic.code)},
        {QStringLiteral("message"),
         fromUtf8(diagnostic.message)},
        {QStringLiteral("object_id"),
         fromUtf8(diagnostic.objectId)},
        {QStringLiteral("source"),
         sourceJson(diagnostic.source)},
    };
}

[[nodiscard]] QJsonArray diagnosticsJson(
    const std::vector<Diagnostic>& diagnostics)
{
    QJsonArray result;
    for (const auto& diagnostic :
         diagnostics) {
        result.append(
            diagnosticJson(
                diagnostic));
    }
    return result;
}

[[nodiscard]] QString fileRevision(
    const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(
            QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(
        QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QStringLiteral("sha256:%1")
        .arg(
            QString::fromLatin1(
                hash.result().toHex()));
}

[[nodiscard]] OpenedProject open(
    const QString& requestedPath)
{
    OpenedProject result;
    result.path =
        QFileInfo(requestedPath)
            .absoluteFilePath();
    result.revision =
        fileRevision(result.path);
    result.open = openProject(
        toPath(result.path));
    if (result.open.manifest) {
        result.path = fromPath(
            result.open.manifest
                ->manifestPath);
        result.revision =
            fileRevision(result.path);
    }
    return result;
}

[[nodiscard]] Diagnostic cliDiagnostic(
    std::string_view code,
    std::string message,
    const QString& project,
    std::string objectId = {})
{
    Diagnostic diagnostic;
    diagnostic.code = std::string(code);
    diagnostic.message = std::move(message);
    diagnostic.objectId =
        std::move(objectId);
    diagnostic.source.workbook =
        toPath(project);
    return diagnostic;
}

[[nodiscard]] bool enforceReadRevision(
    CommandResponse& response,
    const QString& expectedRevision)
{
    if (expectedRevision.isEmpty() ||
        expectedRevision ==
            response.revision) {
        return true;
    }
    response.ok = false;
    response.error =
        QStringLiteral(
            "Project revision does not match --expect; no result was returned.");
    response.diagnostics.push_back(
        cliDiagnostic(
            revisionConflictCode,
            QStringLiteral(
                "Expected revision '%1' but found '%2'.")
                .arg(
                    expectedRevision,
                    response.revision)
                .toUtf8()
                .toStdString(),
            response.project));
    response.result =
        QJsonObject{
            {QStringLiteral(
                 "expected_revision"),
             expectedRevision},
            {QStringLiteral(
                 "current_revision"),
             response.revision},
            {QStringLiteral(
                 "writes_performed"),
             false},
        };
    response.exitCode =
        ExitCode::revisionConflict;
    return false;
}

[[nodiscard]] bool sameDiagnostic(
    const Diagnostic& left,
    const Diagnostic& right)
{
    return left.code == right.code &&
        left.severity == right.severity &&
        left.message == right.message &&
        left.objectId == right.objectId &&
        left.source.workbook ==
            right.source.workbook &&
        left.source.sheet ==
            right.source.sheet &&
        left.source.cell ==
            right.source.cell &&
        left.source.row ==
            right.source.row &&
        left.source.column ==
            right.source.column;
}

void appendUniqueDiagnostics(
    std::vector<Diagnostic>& destination,
    std::vector<Diagnostic> additions)
{
    for (auto& diagnostic : additions) {
        if (std::ranges::none_of(
                destination,
                [&](const Diagnostic& existing) {
                    return sameDiagnostic(
                        existing,
                        diagnostic);
                })) {
            destination.push_back(
                std::move(diagnostic));
        }
    }
}

[[nodiscard]] std::vector<Diagnostic>
withoutMatchingDiagnostics(
    std::vector<Diagnostic> diagnostics,
    const std::vector<Diagnostic>&
        removed)
{
    for (const auto& diagnostic :
         removed) {
        const auto iterator =
            std::ranges::find_if(
                diagnostics,
                [&](const Diagnostic&
                        candidate) {
                    return sameDiagnostic(
                        candidate,
                        diagnostic);
                });
        if (iterator !=
            diagnostics.end()) {
            diagnostics.erase(
                iterator);
        }
    }
    return diagnostics;
}

[[nodiscard]] std::string
diagnosticIssueSignature(
    const Diagnostic& diagnostic)
{
    return diagnostic.code + '\n' +
        diagnostic.objectId + '\n' +
        diagnostic.message;
}

[[nodiscard]] std::multiset<
    std::string, std::less<>>
diagnosticIssueSignatures(
    const std::vector<Diagnostic>&
        diagnostics)
{
    std::multiset<
        std::string, std::less<>>
        result;
    for (const auto& diagnostic :
         diagnostics) {
        result.insert(
            diagnosticIssueSignature(
                diagnostic));
    }
    return result;
}

[[nodiscard]] std::size_t
multisetDifferenceCount(
    std::multiset<
        std::string, std::less<>>
        left,
    const std::multiset<
        std::string, std::less<>>&
        right)
{
    for (const auto& value : right) {
        const auto iterator =
            left.find(value);
        if (iterator != left.end()) {
            left.erase(iterator);
        }
    }
    return left.size();
}

[[nodiscard]] std::size_t
errorCount(
    const std::vector<Diagnostic>&
        diagnostics)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(
            diagnostics,
            [](const Diagnostic&
                   diagnostic) {
                return diagnostic
                           .severity ==
                    DiagnosticSeverity::
                        error;
            }));
}

[[nodiscard]] QJsonObject repairJson(
    const std::vector<Diagnostic>& before,
    const std::vector<Diagnostic>& after)
{
    const auto beforeIssues =
        diagnosticIssueSignatures(
            before);
    const auto afterIssues =
        diagnosticIssueSignatures(
            after);
    return {
        {QStringLiteral(
             "baseline_invalid"),
         hasErrors(before)},
        {QStringLiteral(
             "candidate_valid"),
         !hasErrors(after)},
        {QStringLiteral(
             "before_problem_count"),
         static_cast<qint64>(
             before.size())},
        {QStringLiteral(
             "after_problem_count"),
         static_cast<qint64>(
             after.size())},
        {QStringLiteral(
             "before_error_count"),
         static_cast<qint64>(
             errorCount(before))},
        {QStringLiteral(
             "after_error_count"),
         static_cast<qint64>(
             errorCount(after))},
        {QStringLiteral(
             "resolved_problem_count"),
         static_cast<qint64>(
             multisetDifferenceCount(
                 beforeIssues,
                 afterIssues))},
        {QStringLiteral(
             "introduced_problem_count"),
         static_cast<qint64>(
             multisetDifferenceCount(
                 afterIssues,
                 beforeIssues))},
    };
}

[[nodiscard]] bool requireString(
    const QJsonValue& value,
    std::string& result,
    QString& error)
{
    if (!value.isString()) {
        error =
            QStringLiteral(
                "Value must be a JSON string.");
        return false;
    }
    result =
        value.toString()
            .toUtf8()
            .toStdString();
    return true;
}

[[nodiscard]] bool requireBool(
    const QJsonValue& value,
    bool& result,
    QString& error)
{
    if (!value.isBool()) {
        error =
            QStringLiteral(
                "Value must be a JSON boolean.");
        return false;
    }
    result = value.toBool();
    return true;
}

[[nodiscard]] bool requireUnsignedValue(
    const QJsonValue& value,
    UnsignedValue& result,
    QString& error)
{
    if (value.isString()) {
        const auto parsed =
            UnsignedValue::parse(
                value.toString()
                    .toUtf8()
                    .toStdString());
        if (!parsed) {
            error =
                QStringLiteral(
                    "Value must be an unsigned integer literal such as 0x20 or 32.");
            return false;
        }
        result = *parsed;
        return true;
    }
    if (!value.isDouble()) {
        error =
            QStringLiteral(
                "Value must be an unsigned integer string or a safe JSON integer.");
        return false;
    }
    const double number =
        value.toDouble();
    if (!std::isfinite(number) ||
        number < 0.0 ||
        std::floor(number) != number ||
        number > largestExactJsonInteger) {
        error =
            QStringLiteral(
                "JSON numbers must be non-negative exact integers no larger than 2^53-1; use a string for larger values.");
        return false;
    }
    result = UnsignedValue(
        static_cast<std::uint64_t>(
            number));
    return true;
}

[[nodiscard]] bool requireUInt64(
    const QJsonValue& value,
    std::uint64_t& result,
    QString& error)
{
    UnsignedValue parsed;
    if (!requireUnsignedValue(
            value, parsed, error)) {
        return false;
    }
    const auto converted =
        parsed.toUInt64();
    if (!converted) {
        error =
            QStringLiteral(
                "Value exceeds the unsigned 64-bit range.");
        return false;
    }
    result = *converted;
    return true;
}

[[nodiscard]] bool requireUInt32(
    const QJsonValue& value,
    std::uint32_t& result,
    QString& error)
{
    std::uint64_t parsed = 0;
    if (!requireUInt64(
            value, parsed, error)) {
        return false;
    }
    if (parsed >
        std::numeric_limits<
            std::uint32_t>::max()) {
        error =
            QStringLiteral(
                "Value exceeds the unsigned 32-bit range.");
        return false;
    }
    result =
        static_cast<std::uint32_t>(
            parsed);
    return true;
}

[[nodiscard]] bool optionalTextValue(
    const QJsonValue& value,
    std::optional<std::string>& result,
    QString& error)
{
    if (value.isNull()) {
        result.reset();
        return true;
    }
    std::string parsed;
    if (!requireString(
            value, parsed, error)) {
        error.prepend(
            QStringLiteral(
                "Expected null or a string. "));
        return false;
    }
    result = std::move(parsed);
    return true;
}

[[nodiscard]] bool optionalUnsignedValue(
    const QJsonValue& value,
    std::optional<UnsignedValue>& result,
    QString& error)
{
    if (value.isNull()) {
        result.reset();
        return true;
    }
    UnsignedValue parsed;
    if (!requireUnsignedValue(
            value, parsed, error)) {
        error.prepend(
            QStringLiteral(
                "Expected null or an unsigned value. "));
        return false;
    }
    result = std::move(parsed);
    return true;
}

[[nodiscard]] bool canonicalAccess(
    const QJsonValue& value,
    AccessMode& result,
    QString& error)
{
    std::string token;
    if (!requireString(
            value, token, error)) {
        return false;
    }
    const auto parsed =
        parseAccessMode(token);
    if (!parsed ||
        toString(*parsed) != token) {
        error =
            QStringLiteral(
                "Access must be one of: none, ro, wo, rw.");
        return false;
    }
    result = *parsed;
    return true;
}

[[nodiscard]] bool canonicalFieldType(
    const QJsonValue& value,
    FieldType& result,
    QString& error)
{
    std::string token;
    if (!requireString(
            value, token, error)) {
        return false;
    }
    const auto parsed =
        parseFieldType(token);
    if (!parsed ||
        toString(*parsed) != token) {
        error =
            QStringLiteral(
                "Type must be one of: bits, bool, unsigned, signed, enum, field, reserved.");
        return false;
    }
    result = *parsed;
    return true;
}

[[nodiscard]] bool canonicalReadSideEffect(
    const QJsonValue& value,
    ReadSideEffect& result,
    QString& error)
{
    std::string token;
    if (!requireString(
            value, token, error)) {
        return false;
    }
    const auto parsed =
        parseReadSideEffect(token);
    if (!parsed ||
        toString(*parsed) != token) {
        error =
            QStringLiteral(
                "Read side effect must be one of: none, clear, set.");
        return false;
    }
    result = *parsed;
    return true;
}

[[nodiscard]] bool canonicalWriteSideEffect(
    const QJsonValue& value,
    WriteSideEffect& result,
    QString& error)
{
    std::string token;
    if (!requireString(
            value, token, error)) {
        return false;
    }
    const auto parsed =
        parseWriteSideEffect(token);
    if (!parsed ||
        toString(*parsed) != token) {
        error =
            QStringLiteral(
                "Write side effect must be one of: none, write, w1c, w1s, w0c, w0s, toggle.");
        return false;
    }
    result = *parsed;
    return true;
}

[[nodiscard]] bool stringList(
    const QJsonValue& value,
    std::vector<std::string>& result,
    QString& error)
{
    if (!value.isArray()) {
        error =
            QStringLiteral(
                "Value must be an array of non-empty strings.");
        return false;
    }
    result.clear();
    std::set<std::string, std::less<>>
        unique;
    for (const auto& item :
         value.toArray()) {
        if (!item.isString()) {
            error =
                QStringLiteral(
                    "Every array item must be a string.");
            return false;
        }
        const QString trimmed =
            item.toString().trimmed();
        if (trimmed.isEmpty()) {
            error =
                QStringLiteral(
                    "Tag values cannot be empty.");
            return false;
        }
        std::string tag =
            trimmed.toUtf8()
                .toStdString();
        if (!unique.insert(tag).second) {
            error =
                QStringLiteral(
                    "Tag values must be unique.");
            return false;
        }
        result.push_back(
            std::move(tag));
    }
    return true;
}

[[nodiscard]] QJsonObject enumJson(
    const EnumValue& value)
{
    return {
        {QStringLiteral("kind"),
         QStringLiteral("enum")},
        {QStringLiteral("id"),
         fromUtf8(value.id)},
        {QStringLiteral("name"),
         fromUtf8(value.name)},
        {QStringLiteral("value"),
         fromUtf8(
             value.value
                 .toHexString())},
        {QStringLiteral("description"),
         fromUtf8(
             value.description)},
        {QStringLiteral("source"),
         sourceJson(value.source)},
    };
}

[[nodiscard]] QJsonObject fieldJson(
    const Field& field)
{
    QJsonArray enumValues;
    for (const auto& value :
         field.enumValues) {
        enumValues.append(
            enumJson(value));
    }
    QJsonArray members;
    for (const auto& member :
         field.members) {
        members.append(
            fieldJson(member));
    }
    return {
        {QStringLiteral("kind"),
         QStringLiteral("field")},
        {QStringLiteral("id"),
         fromUtf8(field.id)},
        {QStringLiteral("name"),
         fromUtf8(field.name)},
        {QStringLiteral("msb"),
         static_cast<qint64>(
             field.msb)},
        {QStringLiteral("lsb"),
         static_cast<qint64>(
             field.lsb)},
        {QStringLiteral("width"),
         static_cast<qint64>(
             field.width())},
        {QStringLiteral("type"),
         fromUtf8(
             toString(field.type))},
        {QStringLiteral(
             "software_access"),
         fromUtf8(
             toString(
                 field
                     .softwareAccess))},
        {QStringLiteral(
             "hardware_access"),
         fromUtf8(
             toString(
                 field
                     .hardwareAccess))},
        {QStringLiteral("reset"),
         optionalValue(
             field.resetValue)},
        {QStringLiteral(
             "read_side_effect"),
         fromUtf8(
             toString(
                 field
                     .readSideEffect))},
        {QStringLiteral(
             "write_side_effect"),
         fromUtf8(
             toString(
                 field
                     .writeSideEffect))},
        {QStringLiteral("minimum"),
         optionalText(
             field.minimumValue)},
        {QStringLiteral("maximum"),
         optionalText(
             field.maximumValue)},
        {QStringLiteral("description"),
         fromUtf8(
             field.description)},
        {QStringLiteral("enum_values"),
         enumValues},
        {QStringLiteral("members"),
         members},
        {QStringLiteral("source"),
         sourceJson(field.source)},
    };
}

[[nodiscard]] std::optional<std::uint64_t>
absoluteRegisterAddress(
    const AddressSpace& page,
    const RegisterBlock& block,
    const Register& reg)
{
    if (page.baseAddress >
        std::numeric_limits<
            std::uint64_t>::max() -
            block.baseAddress) {
        return std::nullopt;
    }
    const std::uint64_t blockAddress =
        page.baseAddress +
        block.baseAddress;
    if (blockAddress >
        std::numeric_limits<
            std::uint64_t>::max() -
            reg.offset) {
        return std::nullopt;
    }
    return blockAddress +
        reg.offset;
}

[[nodiscard]] QJsonObject registerJson(
    const AddressSpace& page,
    const RegisterBlock& block,
    const Register& reg)
{
    QJsonArray tags;
    for (const auto& tag : reg.tags) {
        tags.append(fromUtf8(tag));
    }
    QJsonArray enumValues;
    for (const auto& value :
         reg.enumValues) {
        enumValues.append(
            enumJson(value));
    }
    QJsonArray fields;
    for (const auto& field :
         reg.fields) {
        fields.append(
            fieldJson(field));
    }
    const auto address =
        absoluteRegisterAddress(
            page, block, reg);
    return {
        {QStringLiteral("kind"),
         QStringLiteral("register")},
        {QStringLiteral("id"),
         fromUtf8(reg.id)},
        {QStringLiteral("name"),
         fromUtf8(reg.name)},
        {QStringLiteral("page_id"),
         fromUtf8(page.id)},
        {QStringLiteral("block_id"),
         fromUtf8(block.id)},
        {QStringLiteral("offset"),
         hex(reg.offset)},
        {QStringLiteral("address"),
         address
             ? QJsonValue(hex(*address))
             : QJsonValue(
                   QJsonValue::Null)},
        {QStringLiteral("fixed"),
         reg.addressFixed},
        {QStringLiteral("width"),
         static_cast<qint64>(
             reg.width)},
        {QStringLiteral("type"),
         fromUtf8(
             toString(reg.type))},
        {QStringLiteral("minimum"),
         optionalText(
             reg.minimumValue)},
        {QStringLiteral("maximum"),
         optionalText(
             reg.maximumValue)},
        {QStringLiteral("initial"),
         optionalValue(
             reg.initialValue)},
        {QStringLiteral("reset"),
         optionalValue(
             reg.resetValue)},
        {QStringLiteral("access"),
         fromUtf8(
             toString(reg.access))},
        {QStringLiteral("reserved"),
         reg.reserved},
        {QStringLiteral("tags"),
         tags},
        {QStringLiteral("description"),
         fromUtf8(
             reg.description)},
        {QStringLiteral("enum_values"),
         enumValues},
        {QStringLiteral("fields"),
         fields},
        {QStringLiteral("source"),
         sourceJson(reg.source)},
    };
}

[[nodiscard]] QJsonObject blockJson(
    const AddressSpace& page,
    const RegisterBlock& block)
{
    QJsonArray registers;
    for (const auto& reg :
         block.registers) {
        registers.append(
            registerJson(
                page, block, reg));
    }
    return {
        {QStringLiteral("kind"),
         QStringLiteral("block")},
        {QStringLiteral("id"),
         fromUtf8(block.id)},
        {QStringLiteral("name"),
         fromUtf8(block.name)},
        {QStringLiteral("page_id"),
         fromUtf8(page.id)},
        {QStringLiteral("base"),
         hex(block.baseAddress)},
        {QStringLiteral("absolute_base"),
         page.baseAddress <=
                     std::numeric_limits<
                         std::uint64_t>::
                         max() -
                         block.baseAddress
             ? QJsonValue(
                   hex(
                       page.baseAddress +
                       block.baseAddress))
             : QJsonValue(
                   QJsonValue::Null)},
        {QStringLiteral("size"),
         block.size
             ? QJsonValue(
                   hex(*block.size))
             : QJsonValue(
                   QJsonValue::Null)},
        {QStringLiteral("description"),
         fromUtf8(
             block.description)},
        {QStringLiteral("registers"),
         registers},
        {QStringLiteral("source"),
         sourceJson(block.source)},
    };
}

[[nodiscard]] QJsonObject pageJson(
    const AddressSpace& page)
{
    QJsonArray blocks;
    for (const auto& block :
         page.blocks) {
        blocks.append(
            blockJson(page, block));
    }
    return {
        {QStringLiteral("kind"),
         QStringLiteral("page")},
        {QStringLiteral("id"),
         fromUtf8(page.id)},
        {QStringLiteral("name"),
         fromUtf8(page.name)},
        {QStringLiteral("base"),
         hex(page.baseAddress)},
        {QStringLiteral("address_width"),
         static_cast<qint64>(
             page.addressWidth)},
        {QStringLiteral("description"),
         fromUtf8(
             page.description)},
        {QStringLiteral("blocks"),
         blocks},
        {QStringLiteral("source"),
         sourceJson(page.source)},
    };
}

[[nodiscard]] QJsonObject workspaceJson(
    const Workspace& workspace)
{
    QJsonArray pages;
    for (const auto& page :
         workspace.addressSpaces) {
        pages.append(pageJson(page));
    }
    return {
        {QStringLiteral("kind"),
         QStringLiteral(
             "workspace")},
        {QStringLiteral("id"),
         fromUtf8(workspace.id)},
        {QStringLiteral("name"),
         fromUtf8(workspace.name)},
        {QStringLiteral("pages"),
         pages},
    };
}

void countFields(
    const std::vector<Field>& fields,
    int& fieldCount,
    int& enumCount)
{
    for (const auto& field : fields) {
        ++fieldCount;
        enumCount +=
            static_cast<int>(
                field.enumValues.size());
        countFields(
            field.members,
            fieldCount,
            enumCount);
    }
}

[[nodiscard]] QJsonObject summaryJson(
    const Workspace& workspace,
    const ProjectManifest& manifest)
{
    int blockCount = 0;
    int registerCount = 0;
    int fieldCount = 0;
    int enumCount = 0;
    std::map<
        QString,
        int,
        std::less<>>
        tagCounts;
    for (const auto& page :
         workspace.addressSpaces) {
        blockCount +=
            static_cast<int>(
                page.blocks.size());
        for (const auto& block :
             page.blocks) {
            registerCount +=
                static_cast<int>(
                    block.registers
                        .size());
            for (const auto& reg :
                 block.registers) {
                std::set<
                    QString,
                    std::less<>>
                    registerTags;
                for (const auto& value :
                     reg.tags) {
                    const QString tag =
                        fromUtf8(value);
                    if (!tag.isEmpty()) {
                        registerTags.insert(
                            tag);
                    }
                }
                for (const QString& tag :
                     registerTags) {
                    ++tagCounts[tag];
                }
                enumCount +=
                    static_cast<int>(
                        reg.enumValues
                            .size());
                countFields(
                    reg.fields,
                    fieldCount,
                    enumCount);
            }
        }
    }
    QJsonArray targets;
    for (const auto& target :
         manifest.targets) {
        targets.append(
            QJsonObject{
                {QStringLiteral("kind"),
                 fromUtf8(
                     toString(
                         target.kind))},
                {QStringLiteral("path"),
                 fromPath(
                     target.path
                         .resolved)},
            });
    }
    QJsonArray tags;
    for (const auto& [name, count] :
         tagCounts) {
        tags.append(
            QJsonObject{
                {QStringLiteral("name"),
                 name},
                {QStringLiteral(
                     "register_count"),
                 count},
            });
    }
    return {
        {QStringLiteral("workspace_id"),
         fromUtf8(workspace.id)},
        {QStringLiteral("workspace_name"),
         fromUtf8(workspace.name)},
        {QStringLiteral("counts"),
         QJsonObject{
             {QStringLiteral("pages"),
              static_cast<int>(
                  workspace
                      .addressSpaces
                      .size())},
             {QStringLiteral("blocks"),
              blockCount},
             {QStringLiteral("registers"),
              registerCount},
             {QStringLiteral("fields"),
              fieldCount},
             {QStringLiteral("enum_values"),
              enumCount},
             {QStringLiteral("tags"),
              static_cast<int>(
                  tagCounts.size())},
         }},
        {QStringLiteral("tags"),
         tags},
        {QStringLiteral("generation_targets"),
         targets},
    };
}

void addDescriptor(
    std::vector<ObjectDescriptor>& objects,
    QString kind,
    std::string_view id,
    std::string_view name,
    std::string_view parentId,
    QString path,
    QJsonObject extra = {})
{
    objects.push_back(
        ObjectDescriptor{
            std::move(kind),
            fromUtf8(id),
            fromUtf8(name),
            fromUtf8(parentId),
            std::move(path),
            std::move(extra)});
}

void describeFields(
    const std::vector<Field>& fields,
    std::string_view parentId,
    const QString& parentPath,
    std::vector<ObjectDescriptor>& objects)
{
    for (const auto& field : fields) {
        const QString path =
            parentPath +
            QStringLiteral("/") +
            fromUtf8(field.name);
        addDescriptor(
            objects,
            QStringLiteral("field"),
            field.id, field.name,
            parentId, path,
            QJsonObject{
                {QStringLiteral("msb"),
                 static_cast<qint64>(
                     field.msb)},
                {QStringLiteral("lsb"),
                 static_cast<qint64>(
                     field.lsb)},
                {QStringLiteral("width"),
                 static_cast<qint64>(
                     field.width())},
                {QStringLiteral("type"),
                 fromUtf8(
                     toString(
                         field.type))},
                {QStringLiteral(
                     "description"),
                 fromUtf8(
                     field.description)},
            });
        for (const auto& value :
             field.enumValues) {
            addDescriptor(
                objects,
                QStringLiteral("enum"),
                value.id, value.name,
                field.id,
                path +
                    QStringLiteral("/") +
                    fromUtf8(
                        value.name),
                QJsonObject{
                    {QStringLiteral("value"),
                     fromUtf8(
                         value.value
                             .toHexString())},
                    {QStringLiteral(
                         "description"),
                     fromUtf8(
                         value.description)},
                });
        }
        describeFields(
            field.members,
            field.id, path,
            objects);
    }
}

[[nodiscard]] std::vector<ObjectDescriptor>
describeObjects(
    const Workspace& workspace)
{
    std::vector<ObjectDescriptor> result;
    addDescriptor(
        result,
        QStringLiteral("workspace"),
        workspace.id,
        workspace.name, {},
        fromUtf8(workspace.name));
    for (const auto& page :
         workspace.addressSpaces) {
        const QString pagePath =
            fromUtf8(workspace.name) +
            QStringLiteral("/") +
            fromUtf8(page.name);
        addDescriptor(
            result,
            QStringLiteral("page"),
            page.id, page.name,
            workspace.id, pagePath,
            QJsonObject{
                {QStringLiteral("base"),
                 hex(page.baseAddress)},
                {QStringLiteral(
                     "address_width"),
                 static_cast<qint64>(
                     page.addressWidth)},
                {QStringLiteral(
                     "description"),
                 fromUtf8(
                     page.description)},
            });
        for (const auto& block :
             page.blocks) {
            const QString blockPath =
                pagePath +
                QStringLiteral("/") +
                fromUtf8(block.name);
            addDescriptor(
                result,
                QStringLiteral("block"),
                block.id, block.name,
                page.id, blockPath,
                QJsonObject{
                    {QStringLiteral("base"),
                     hex(
                         block
                             .baseAddress)},
                    {QStringLiteral("size"),
                     block.size
                         ? QJsonValue(
                               hex(
                                   *block
                                        .size))
                         : QJsonValue(
                               QJsonValue::
                                   Null)},
                    {QStringLiteral(
                         "description"),
                     fromUtf8(
                         block.description)},
                });
            for (const auto& reg :
                 block.registers) {
                const QString registerPath =
                    blockPath +
                    QStringLiteral("/") +
                    fromUtf8(reg.name);
                const auto address =
                    absoluteRegisterAddress(
                        page, block, reg);
                QJsonArray tags;
                for (const auto& tag :
                     reg.tags) {
                    tags.append(
                        fromUtf8(tag));
                }
                addDescriptor(
                    result,
                    QStringLiteral(
                        "register"),
                    reg.id, reg.name,
                    block.id,
                    registerPath,
                    QJsonObject{
                        {QStringLiteral(
                             "offset"),
                         hex(reg.offset)},
                        {QStringLiteral(
                             "address"),
                         address
                             ? QJsonValue(
                                   hex(
                                       *address))
                             : QJsonValue(
                                   QJsonValue::
                                       Null)},
                        {QStringLiteral(
                             "width"),
                         static_cast<qint64>(
                             reg.width)},
                        {QStringLiteral(
                             "type"),
                         fromUtf8(
                             toString(
                                 reg.type))},
                        {QStringLiteral(
                             "tags"),
                         tags},
                        {QStringLiteral(
                             "description"),
                         fromUtf8(
                             reg.description)},
                    });
                for (const auto& value :
                     reg.enumValues) {
                    addDescriptor(
                        result,
                        QStringLiteral(
                            "enum"),
                        value.id,
                        value.name,
                        reg.id,
                        registerPath +
                            QStringLiteral(
                                "/") +
                            fromUtf8(
                                value.name),
                        QJsonObject{
                            {QStringLiteral(
                                 "value"),
                             fromUtf8(
                                 value.value
                                     .toHexString())},
                            {QStringLiteral(
                                 "description"),
                             fromUtf8(
                                 value.description)},
                        });
                }
                describeFields(
                    reg.fields,
                    reg.id,
                    registerPath,
                    result);
            }
        }
    }
    return result;
}

[[nodiscard]] std::optional<ObjectDescriptor>
describeObject(
    const Workspace& workspace,
    const QString& id)
{
    const auto objects =
        describeObjects(workspace);
    const auto iterator =
        std::ranges::find_if(
            objects,
            [&id](
                const ObjectDescriptor&
                    object) {
                return object.id == id;
            });
    if (iterator == objects.end()) {
        return std::nullopt;
    }
    return *iterator;
}

[[nodiscard]] QJsonObject descriptorJson(
    const ObjectDescriptor& object)
{
    QJsonObject result{
        {QStringLiteral("kind"),
         object.kind},
        {QStringLiteral("id"),
         object.id},
        {QStringLiteral("name"),
         object.name},
        {QStringLiteral("parent_id"),
         object.parentId.isEmpty()
             ? QJsonValue(
                   QJsonValue::Null)
             : QJsonValue(
                   object.parentId)},
        {QStringLiteral("path"),
         object.path},
    };
    for (auto iterator =
             object.extra.begin();
         iterator !=
         object.extra.end();
         ++iterator) {
        result.insert(
            iterator.key(),
            iterator.value());
    }
    return result;
}

[[nodiscard]] bool descriptorHasTag(
    const ObjectDescriptor& object,
    const QString& tag)
{
    if (tag.isEmpty()) {
        return true;
    }
    const QJsonArray tags =
        object.extra
            .value(
                QStringLiteral("tags"))
            .toArray();
    return std::ranges::any_of(
        tags,
        [&tag](
            const QJsonValue& value) {
            return value.toString() ==
                tag;
        });
}

using DescriptorParents =
    std::map<
        QString,
        QString,
        std::less<>>;

[[nodiscard]] DescriptorParents descriptorParents(
    const std::vector<ObjectDescriptor>& objects)
{
    DescriptorParents result;
    for (const ObjectDescriptor& object :
         objects) {
        result.insert_or_assign(
            object.id,
            object.parentId);
    }
    return result;
}

[[nodiscard]] bool descriptorInParentScope(
    const ObjectDescriptor& object,
    const QString& parent,
    bool recursive,
    const DescriptorParents& parents)
{
    if (parent.isEmpty()) {
        return true;
    }
    if (!recursive) {
        return object.parentId == parent;
    }

    QString current = object.parentId;
    for (std::size_t depth = 0;
         depth < parents.size() &&
         !current.isEmpty();
         ++depth) {
        if (current == parent) {
            return true;
        }
        const auto found =
            parents.find(current);
        if (found == parents.end()) {
            return false;
        }
        current = found->second;
    }
    return false;
}

[[nodiscard]] bool jsonValueMatches(
    const QJsonValue& value,
    const QString& query,
    bool exact)
{
    if (value.isString()) {
        return exact
            ? value.toString().compare(
                  query,
                  Qt::CaseInsensitive) == 0
            : value.toString().contains(
                  query,
                  Qt::CaseInsensitive);
    }
    if (value.isDouble()) {
        const QString text =
            QString::number(
                value.toDouble(),
                'g', 16);
        return exact
            ? text.compare(
                  query,
                  Qt::CaseInsensitive) == 0
            : text.contains(
                  query,
                  Qt::CaseInsensitive);
    }
    if (value.isBool()) {
        const QString text =
            value.toBool()
            ? QStringLiteral("true")
            : QStringLiteral("false");
        return exact
            ? text.compare(
                  query,
                  Qt::CaseInsensitive) == 0
            : text.contains(
                  query,
                  Qt::CaseInsensitive);
    }
    if (value.isArray()) {
        return std::ranges::any_of(
            value.toArray(),
            [&](const QJsonValue&
                    member) {
                return jsonValueMatches(
                    member,
                    query,
                    exact);
            });
    }
    if (value.isObject()) {
        const QJsonObject object =
            value.toObject();
        return std::ranges::any_of(
            object,
            [&](const QJsonValue&
                    member) {
                return jsonValueMatches(
                    member,
                    query,
                    exact);
            });
    }
    return false;
}

[[nodiscard]] std::optional<QString>
matchingExtraField(
    const QJsonObject& extra,
    const QString& query,
    bool exact)
{
    for (auto iterator =
             extra.constBegin();
         iterator !=
         extra.constEnd();
         ++iterator) {
        if (jsonValueMatches(
                iterator.value(),
                query,
                exact)) {
            return iterator.key();
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SearchMatch>
matchObject(
    const ObjectDescriptor& object,
    const QString& query)
{
    const auto equals =
        [&query](const QString& value) {
            return value.compare(
                       query,
                       Qt::CaseInsensitive) ==
                0;
        };
    const auto startsWith =
        [&query](const QString& value) {
            return value.startsWith(
                query,
                Qt::CaseInsensitive);
        };
    if (equals(object.id)) {
        return SearchMatch{
            0,
            QStringLiteral("id")};
    }
    if (equals(object.name)) {
        return SearchMatch{
            0,
            QStringLiteral("name")};
    }
    if (equals(object.path)) {
        return SearchMatch{
            1,
            QStringLiteral("path")};
    }
    if (const auto field =
            matchingExtraField(
                object.extra,
                query,
                true)) {
        return SearchMatch{
            1, *field};
    }
    if (startsWith(object.id)) {
        return SearchMatch{
            2,
            QStringLiteral("id")};
    }
    if (startsWith(object.name)) {
        return SearchMatch{
            2,
            QStringLiteral("name")};
    }
    if (object.id.contains(
            query,
            Qt::CaseInsensitive)) {
        return SearchMatch{
            3,
            QStringLiteral("id")};
    }
    if (object.name.contains(
            query,
            Qt::CaseInsensitive)) {
        return SearchMatch{
            3,
            QStringLiteral("name")};
    }
    if (object.path.contains(
            query,
            Qt::CaseInsensitive)) {
        return SearchMatch{
            3,
            QStringLiteral("path")};
    }
    if (const auto field =
            matchingExtraField(
                object.extra,
                query,
                false)) {
        return SearchMatch{
            3, *field};
    }
    return std::nullopt;
}

[[nodiscard]] QJsonValue findObjectJson(
    const Workspace& workspace,
    std::string_view id)
{
    if (workspace.id == id) {
        return workspaceJson(workspace);
    }
    for (const auto& page :
         workspace.addressSpaces) {
        if (page.id == id) {
            return pageJson(page);
        }
        for (const auto& block :
             page.blocks) {
            if (block.id == id) {
                return blockJson(
                    page, block);
            }
            for (const auto& reg :
                 block.registers) {
                if (reg.id == id) {
                    return registerJson(
                        page, block, reg);
                }
                for (const auto& value :
                     reg.enumValues) {
                    if (value.id == id) {
                        return enumJson(
                            value);
                    }
                }
                const auto findField =
                    [&](const auto& self,
                        const std::vector<Field>&
                            fields)
                    -> QJsonValue {
                    for (const auto& field :
                         fields) {
                        if (field.id == id) {
                            return fieldJson(
                                field);
                        }
                        for (const auto& value :
                             field.enumValues) {
                            if (value.id == id) {
                                return enumJson(
                                    value);
                            }
                        }
                        const QJsonValue nested =
                            self(
                                self,
                                field.members);
                        if (!nested.isUndefined()) {
                            return nested;
                        }
                    }
                    return QJsonValue(
                        QJsonValue::Undefined);
                };
                const QJsonValue field =
                    findField(
                        findField,
                        reg.fields);
                if (!field.isUndefined()) {
                    return field;
                }
            }
        }
    }
    return QJsonValue(
        QJsonValue::Undefined);
}

[[nodiscard]] bool setWorkspaceProperty(
    Workspace& workspace,
    const QString& property,
    const QJsonValue& value,
    QString& error)
{
    if (property ==
        QStringLiteral("name")) {
        return requireString(
            value,
            workspace.name,
            error);
    }
    error =
        QStringLiteral(
            "Property '%1' is not writable on a workspace. Writable properties: name.")
            .arg(property);
    return false;
}

[[nodiscard]] bool setPageProperty(
    AddressSpace& page,
    const QString& property,
    const QJsonValue& value,
    QString& error)
{
    if (property ==
        QStringLiteral("name")) {
        return requireString(
            value, page.name, error);
    }
    if (property ==
        QStringLiteral("base")) {
        return requireUInt64(
            value,
            page.baseAddress,
            error);
    }
    if (property ==
        QStringLiteral(
            "address_width")) {
        return requireUInt32(
            value,
            page.addressWidth,
            error);
    }
    if (property ==
        QStringLiteral(
            "description")) {
        return requireString(
            value,
            page.description,
            error);
    }
    error =
        QStringLiteral(
            "Property '%1' is not writable on a page. Writable properties: name, base, address_width, description.")
            .arg(property);
    return false;
}

[[nodiscard]] bool setBlockProperty(
    RegisterBlock& block,
    const QString& property,
    const QJsonValue& value,
    QString& error)
{
    if (property ==
        QStringLiteral("name")) {
        return requireString(
            value, block.name, error);
    }
    if (property ==
        QStringLiteral("base")) {
        return requireUInt64(
            value,
            block.baseAddress,
            error);
    }
    if (property ==
        QStringLiteral("size")) {
        if (value.isNull()) {
            block.size.reset();
            return true;
        }
        std::uint64_t size = 0;
        if (!requireUInt64(
                value, size, error)) {
            error.prepend(
                QStringLiteral(
                    "Expected null or a block size. "));
            return false;
        }
        block.size = size;
        return true;
    }
    if (property ==
        QStringLiteral(
            "description")) {
        return requireString(
            value,
            block.description,
            error);
    }
    error =
        QStringLiteral(
            "Property '%1' is not writable on a block. Writable properties: name, base, size, description.")
            .arg(property);
    return false;
}

[[nodiscard]] bool setRegisterProperty(
    Register& reg,
    const QString& property,
    const QJsonValue& value,
    QString& error)
{
    if (property ==
        QStringLiteral("name")) {
        return requireString(
            value, reg.name, error);
    }
    if (property ==
        QStringLiteral("offset")) {
        return requireUInt64(
            value, reg.offset, error);
    }
    if (property ==
        QStringLiteral("fixed")) {
        return requireBool(
            value,
            reg.addressFixed,
            error);
    }
    if (property ==
        QStringLiteral("width")) {
        return requireUInt32(
            value, reg.width, error);
    }
    if (property ==
        QStringLiteral("type")) {
        return canonicalFieldType(
            value, reg.type, error);
    }
    if (property ==
        QStringLiteral("minimum")) {
        return optionalTextValue(
            value,
            reg.minimumValue,
            error);
    }
    if (property ==
        QStringLiteral("maximum")) {
        return optionalTextValue(
            value,
            reg.maximumValue,
            error);
    }
    if (property ==
        QStringLiteral("initial")) {
        return optionalUnsignedValue(
            value,
            reg.initialValue,
            error);
    }
    if (property ==
        QStringLiteral("reset")) {
        return optionalUnsignedValue(
            value,
            reg.resetValue,
            error);
    }
    if (property ==
        QStringLiteral("access")) {
        return canonicalAccess(
            value, reg.access, error);
    }
    if (property ==
        QStringLiteral("reserved")) {
        return requireBool(
            value, reg.reserved, error);
    }
    if (property ==
        QStringLiteral("tags")) {
        return stringList(
            value, reg.tags, error);
    }
    if (property ==
        QStringLiteral(
            "description")) {
        return requireString(
            value,
            reg.description,
            error);
    }
    error =
        QStringLiteral(
            "Property '%1' is not writable on a register. Writable properties: name, offset, fixed, width, type, minimum, maximum, initial, reset, access, reserved, tags, description.")
            .arg(property);
    return false;
}

[[nodiscard]] bool setFieldProperty(
    Field& field,
    const QString& property,
    const QJsonValue& value,
    QString& error)
{
    if (property ==
        QStringLiteral("name")) {
        return requireString(
            value, field.name, error);
    }
    if (property ==
        QStringLiteral("lsb")) {
        std::uint32_t lsb = 0;
        if (!requireUInt32(
                value, lsb, error)) {
            return false;
        }
        const std::uint64_t width =
            field.width();
        if (width == 0) {
            error =
                QStringLiteral(
                    "Field has an invalid current bit range; LSB cannot be repositioned.");
            return false;
        }
        const std::uint64_t msb =
            static_cast<std::uint64_t>(
                lsb) +
            width - 1;
        if (msb >
            std::numeric_limits<
                std::uint32_t>::max()) {
            error =
                QStringLiteral(
                    "Field LSB places the MSB outside the unsigned 32-bit range.");
            return false;
        }
        field.lsb = lsb;
        field.msb =
            static_cast<std::uint32_t>(
                msb);
        return true;
    }
    if (property ==
        QStringLiteral("msb")) {
        return requireUInt32(
            value, field.msb, error);
    }
    if (property ==
        QStringLiteral("width")) {
        std::uint32_t width = 0;
        if (!requireUInt32(
                value, width, error)) {
            return false;
        }
        if (width == 0) {
            error =
                QStringLiteral(
                    "Field width must be greater than zero.");
            return false;
        }
        const std::uint64_t msb =
            static_cast<std::uint64_t>(
                field.lsb) +
            width - 1;
        if (msb >
            std::numeric_limits<
                std::uint32_t>::max()) {
            error =
                QStringLiteral(
                    "Field width places the MSB outside the unsigned 32-bit range.");
            return false;
        }
        field.msb =
            static_cast<std::uint32_t>(
                msb);
        return true;
    }
    if (property ==
        QStringLiteral("type")) {
        return canonicalFieldType(
            value, field.type, error);
    }
    if (property ==
        QStringLiteral(
            "software_access")) {
        return canonicalAccess(
            value,
            field.softwareAccess,
            error);
    }
    if (property ==
        QStringLiteral(
            "hardware_access")) {
        return canonicalAccess(
            value,
            field.hardwareAccess,
            error);
    }
    if (property ==
        QStringLiteral("reset")) {
        error = QStringLiteral(
            "Field Reset is derived from its containing Register Reset and cannot be written independently. Set 'reset' on the parent Register instead.");
        return false;
    }
    if (property ==
        QStringLiteral(
            "read_side_effect")) {
        return canonicalReadSideEffect(
            value,
            field.readSideEffect,
            error);
    }
    if (property ==
        QStringLiteral(
            "write_side_effect")) {
        return canonicalWriteSideEffect(
            value,
            field.writeSideEffect,
            error);
    }
    if (property ==
        QStringLiteral("minimum")) {
        return optionalTextValue(
            value,
            field.minimumValue,
            error);
    }
    if (property ==
        QStringLiteral("maximum")) {
        return optionalTextValue(
            value,
            field.maximumValue,
            error);
    }
    if (property ==
        QStringLiteral(
            "description")) {
        return requireString(
            value,
            field.description,
            error);
    }
    error =
        QStringLiteral(
            "Property '%1' is not writable on a field. Writable properties: name, lsb, msb, width, type, software_access, hardware_access, read_side_effect, write_side_effect, minimum, maximum, description.")
            .arg(property);
    return false;
}

[[nodiscard]] bool setEnumProperty(
    EnumValue& enumValue,
    const QString& property,
    const QJsonValue& value,
    QString& error)
{
    if (property ==
        QStringLiteral("name")) {
        return requireString(
            value,
            enumValue.name,
            error);
    }
    if (property ==
        QStringLiteral("value")) {
        return requireUnsignedValue(
            value,
            enumValue.value,
            error);
    }
    if (property ==
        QStringLiteral(
            "description")) {
        return requireString(
            value,
            enumValue.description,
            error);
    }
    error =
        QStringLiteral(
            "Property '%1' is not writable on an enum value. Writable properties: name, value, description.")
            .arg(property);
    return false;
}

void refreshWorkspaceFieldResetValues(
    Workspace& workspace);

[[nodiscard]] bool setObjectProperty(
    Workspace& workspace,
    const QString& id,
    const QString& property,
    const QJsonValue& value,
    QString& kind,
    QString& error)
{
    const std::string objectId =
        id.toUtf8().toStdString();
    if (workspace.id == objectId) {
        kind =
            QStringLiteral(
                "workspace");
        return setWorkspaceProperty(
            workspace,
            property,
            value,
            error);
    }
    if (auto* page =
            findAddressSpace(
                workspace, objectId)) {
        kind =
            QStringLiteral("page");
        return setPageProperty(
            *page,
            property,
            value,
            error);
    }
    if (auto* block =
            findRegisterBlock(
                workspace, objectId)) {
        kind =
            QStringLiteral("block");
        return setBlockProperty(
            *block,
            property,
            value,
            error);
    }
    if (auto* reg =
            findRegister(
                workspace, objectId)) {
        kind =
            QStringLiteral(
                "register");
        const bool updated =
            setRegisterProperty(
            *reg,
            property,
            value,
            error);
        if (updated) {
            refreshWorkspaceFieldResetValues(
                workspace);
        }
        return updated;
    }
    if (auto* field =
            findField(
                workspace, objectId)) {
        kind =
            QStringLiteral("field");
        const bool updated =
            setFieldProperty(
            *field,
            property,
            value,
            error);
        if (updated) {
            refreshWorkspaceFieldResetValues(
                workspace);
        }
        return updated;
    }
    if (auto* enumValue =
            findEnumValue(
                workspace, objectId)) {
        kind =
            QStringLiteral("enum");
        return setEnumProperty(
            *enumValue,
            property,
            value,
            error);
    }
    error =
        QStringLiteral(
            "No workspace object has stable ID '%1'.")
            .arg(id);
    return false;
}

template <typename Value>
void sortByOffsetAndId(
    std::vector<Value>& values)
{
    std::ranges::stable_sort(
        values,
        [](const Value& left,
           const Value& right) {
            return std::tie(
                       left.offset,
                       left.id) <
                std::tie(
                       right.offset,
                       right.id);
        });
}

[[nodiscard]] bool placementAdditionOverflows(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result)
{
    if (right >
        std::numeric_limits<std::uint64_t>::max() -
            left) {
        return true;
    }
    result = left + right;
    return false;
}

[[nodiscard]] std::optional<std::uint64_t>
registerPlacementExtent(
    const Register& reg)
{
    static_cast<void>(reg);
    return std::uint64_t{4};
}

struct BlockPlacementInterval {
    std::uint64_t first{0};
    std::uint64_t last{0};
};

[[nodiscard]] std::optional<std::uint64_t>
blockPlacementAllocation(
    const RegisterBlock& block)
{
    if (block.size.has_value() &&
        *block.size == 0) {
        return std::nullopt;
    }
    std::uint64_t allocation =
        block.size.value_or(0);
    for (const auto& reg :
         block.registers) {
        const auto extent =
            registerPlacementExtent(reg);
        std::uint64_t end = 0;
        if (!extent ||
            placementAdditionOverflows(
                reg.offset,
                *extent,
                end)) {
            return std::nullopt;
        }
        allocation =
            std::max(allocation, end);
    }
    return allocation == 0
        ? std::optional<std::uint64_t>{
              1}
        : std::optional{allocation};
}

[[nodiscard]] std::optional<
    BlockPlacementInterval>
blockPlacementInterval(
    const RegisterBlock& block)
{
    const auto allocation =
        blockPlacementAllocation(block);
    std::uint64_t last = 0;
    if (!allocation ||
        placementAdditionOverflows(
            block.baseAddress,
            *allocation - 1,
            last)) {
        return std::nullopt;
    }
    return BlockPlacementInterval{
        block.baseAddress, last};
}

[[nodiscard]] bool blockPlacementFitsPage(
    const AddressSpace& page,
    const RegisterBlock& block)
{
    const auto interval =
        blockPlacementInterval(block);
    std::uint64_t absoluteFirst = 0;
    std::uint64_t absoluteLast = 0;
    if (!interval ||
        page.addressWidth == 0 ||
        page.addressWidth > 64 ||
        placementAdditionOverflows(
            page.baseAddress,
            interval->first,
            absoluteFirst) ||
        placementAdditionOverflows(
            page.baseAddress,
            interval->last,
            absoluteLast)) {
        return false;
    }
    if (page.addressWidth == 64) {
        return true;
    }
    return absoluteLast <
        (std::uint64_t{1}
         << page.addressWidth);
}

[[nodiscard]] bool blockIntervalsOverlap(
    const BlockPlacementInterval& left,
    const BlockPlacementInterval& right)
{
    return left.first <= right.last &&
        right.first <= left.last;
}

[[nodiscard]] bool blockPlacementAvailable(
    const AddressSpace& page,
    const RegisterBlock& candidate)
{
    const auto candidateInterval =
        blockPlacementInterval(
            candidate);
    if (!candidateInterval ||
        !blockPlacementFitsPage(
            page, candidate)) {
        return false;
    }
    for (const auto& existing :
         page.blocks) {
        const auto occupied =
            blockPlacementInterval(
                existing);
        if (!occupied ||
            blockIntervalsOverlap(
                *candidateInterval,
                *occupied)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t>
automaticBlockBase(
    const AddressSpace& page,
    const RegisterBlock& block)
{
    RegisterBlock candidate = block;
    candidate.baseAddress = 0;
    for (std::size_t attempt = 0;
         attempt <= page.blocks.size();
         ++attempt) {
        if (blockPlacementAvailable(
                page, candidate)) {
            return candidate.baseAddress;
        }
        const auto candidateInterval =
            blockPlacementInterval(
                candidate);
        if (!candidateInterval) {
            return std::nullopt;
        }
        std::optional<std::uint64_t>
            nextBase;
        for (const auto& existing :
             page.blocks) {
            const auto occupied =
                blockPlacementInterval(
                    existing);
            if (!occupied) {
                return std::nullopt;
            }
            if (!blockIntervalsOverlap(
                    *candidateInterval,
                    *occupied)) {
                continue;
            }
            if (occupied->last ==
                std::numeric_limits<
                    std::uint64_t>::max()) {
                return std::nullopt;
            }
            nextBase =
                std::max(
                    nextBase.value_or(0),
                    occupied->last + 1);
        }
        if (!nextBase ||
            *nextBase <=
                candidate.baseAddress) {
            return std::nullopt;
        }
        candidate.baseAddress =
            *nextBase;
    }
    return std::nullopt;
}

[[nodiscard]] const AddressSpace*
addressSpaceContainingBlock(
    const Workspace& workspace,
    std::string_view blockId)
{
    for (const auto& page :
         workspace.addressSpaces) {
        if (std::ranges::any_of(
                page.blocks,
                [blockId](
                    const RegisterBlock&
                        block) {
                    return block.id ==
                        blockId;
                })) {
            return &page;
        }
    }
    return nullptr;
}

struct RegisterPlacementLimit {
    bool valid{false};
    std::optional<std::uint64_t>
        exclusiveEnd;
};

[[nodiscard]] RegisterPlacementLimit
registerPlacementLimit(
    const Workspace& workspace,
    const RegisterBlock& block)
{
    const AddressSpace* page =
        addressSpaceContainingBlock(
            workspace, block.id);
    if (page == nullptr ||
        page->addressWidth == 0 ||
        page->addressWidth > 64 ||
        (block.size.has_value() &&
         *block.size == 0)) {
        return {};
    }

    std::uint64_t absoluteBlockBase = 0;
    if (placementAdditionOverflows(
            page->baseAddress,
            block.baseAddress,
            absoluteBlockBase)) {
        return {};
    }

    std::optional<std::uint64_t>
        pageCapacity;
    if (page->addressWidth < 64) {
        const std::uint64_t pageEnd =
            std::uint64_t{1}
            << page->addressWidth;
        if (absoluteBlockBase >=
            pageEnd) {
            return {};
        }
        pageCapacity =
            pageEnd -
            absoluteBlockBase;
    } else if (absoluteBlockBase != 0) {
        pageCapacity =
            std::numeric_limits<
                std::uint64_t>::max() -
            absoluteBlockBase +
            1U;
    }

    std::optional<std::uint64_t> limit =
        block.size;
    if (pageCapacity &&
        (!limit ||
         *pageCapacity < *limit)) {
        limit = pageCapacity;
    }
    return RegisterPlacementLimit{
        true, limit};
}

[[nodiscard]] std::optional<std::uint64_t>
availableAutomaticRegisterOffset(
    const RegisterBlock& block,
    std::uint64_t desiredStart,
    std::uint64_t span,
    const RegisterPlacementLimit& limit)
{
    if (!limit.valid || span == 0) {
        return std::nullopt;
    }
    const auto align =
        [](std::uint64_t value)
        -> std::optional<std::uint64_t> {
        constexpr std::uint64_t
            alignment = 4;
        const std::uint64_t remainder =
            value % alignment;
        if (remainder == 0) {
            return value;
        }
        std::uint64_t aligned = 0;
        return placementAdditionOverflows(
                   value,
                   alignment - remainder,
                   aligned)
            ? std::nullopt
            : std::optional{aligned};
    };
    auto candidate =
        align(desiredStart);
    if (!candidate) {
        return std::nullopt;
    }

    std::vector<std::pair<
        std::uint64_t,
        std::uint64_t>> occupied;
    occupied.reserve(
        block.registers.size());
    for (const auto& existing :
         block.registers) {
        const auto extent =
            registerPlacementExtent(
                existing);
        std::uint64_t end = 0;
        if (!extent ||
            placementAdditionOverflows(
                existing.offset,
                *extent,
                end)) {
            return std::nullopt;
        }
        occupied.emplace_back(
            existing.offset, end);
    }
    std::ranges::sort(occupied);

    const auto fits =
        [&limit, span](
            std::uint64_t start) {
            std::uint64_t end = 0;
            return !placementAdditionOverflows(
                       start, span, end) &&
                (!limit.exclusiveEnd ||
                 end <=
                     *limit.exclusiveEnd);
        };
    for (const auto& [first, last] :
         occupied) {
        if (last <= *candidate) {
            continue;
        }
        std::uint64_t candidateEnd = 0;
        if (!placementAdditionOverflows(
                *candidate,
                span,
                candidateEnd) &&
            candidateEnd <= first &&
            fits(*candidate)) {
            return candidate;
        }
        candidate =
            align(std::max(
                *candidate, last));
        if (!candidate) {
            return std::nullopt;
        }
    }
    return fits(*candidate)
        ? candidate
        : std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t>
automaticRegisterOffset(
    const Workspace& workspace,
    const RegisterBlock& block,
    const Register& reg)
{
    const auto span =
        registerPlacementExtent(reg);
    const RegisterPlacementLimit limit =
        registerPlacementLimit(
            workspace, block);
    if (!span || !limit.valid) {
        return std::nullopt;
    }

    std::uint64_t appendOffset = 0;
    for (const auto& existing :
         block.registers) {
        const auto extent =
            registerPlacementExtent(
                existing);
        std::uint64_t end = 0;
        if (!extent ||
            placementAdditionOverflows(
                existing.offset,
                *extent,
                end)) {
            return std::nullopt;
        }
        appendOffset =
            std::max(
                appendOffset, end);
    }
    if (const auto appended =
            availableAutomaticRegisterOffset(
                block,
                appendOffset,
                *span,
                limit)) {
        return appended;
    }
    return appendOffset == 0
        ? std::nullopt
        : availableAutomaticRegisterOffset(
              block, 0, *span, limit);
}

void sortFieldsByBit(
    std::vector<Field>& fields)
{
    std::ranges::stable_sort(
        fields,
        [](const Field& left,
           const Field& right) {
            return std::tie(
                       left.lsb,
                       left.msb,
                       left.id) <
                std::tie(
                       right.lsb,
                       right.msb,
                       right.id);
        });
}

[[nodiscard]] std::optional<std::uint32_t>
automaticFieldLsb(
    const std::vector<Field>& fields,
    std::uint64_t desiredWidth,
    std::uint64_t containerWidth)
{
    if (desiredWidth == 0 ||
        desiredWidth > containerWidth) {
        return std::nullopt;
    }

    std::vector<std::pair<
        std::uint64_t,
        std::uint64_t>> occupied;
    occupied.reserve(fields.size());
    for (const auto& existing :
         fields) {
        if (existing.msb <
                existing.lsb ||
            existing.msb >=
                containerWidth) {
            return std::nullopt;
        }
        occupied.emplace_back(
            existing.lsb,
            existing.msb);
    }
    std::ranges::sort(occupied);

    std::uint64_t cursor = 0;
    for (const auto& [first, last] :
         occupied) {
        if (last < cursor) {
            continue;
        }
        if (first > cursor &&
            desiredWidth <=
                first - cursor) {
            return cursor <=
                       std::numeric_limits<
                           std::uint32_t>::max()
                ? std::optional{
                      static_cast<
                          std::uint32_t>(
                          cursor)}
                : std::nullopt;
        }
        if (last ==
            std::numeric_limits<
                std::uint64_t>::max()) {
            return std::nullopt;
        }
        cursor =
            std::max(cursor, last + 1);
        if (cursor >=
            containerWidth) {
            return std::nullopt;
        }
    }
    if (cursor >
            std::numeric_limits<
                std::uint32_t>::max() ||
        desiredWidth >
            containerWidth - cursor) {
        return std::nullopt;
    }
    const std::uint64_t msb =
        cursor + desiredWidth - 1;
    return msb <=
               std::numeric_limits<
                   std::uint32_t>::max()
        ? std::optional{
              static_cast<std::uint32_t>(
                  cursor)}
        : std::nullopt;
}

[[nodiscard]] bool fieldTreeContains(
    const std::vector<Field>& fields,
    std::string_view fieldId)
{
    for (const auto& field : fields) {
        if (field.id == fieldId ||
            fieldTreeContains(
                field.members,
                fieldId)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Register*
fieldOwnerRegister(
    Workspace& workspace,
    std::string_view fieldId)
{
    for (auto& page :
         workspace.addressSpaces) {
        for (auto& block :
             page.blocks) {
            for (auto& reg :
                 block.registers) {
                if (fieldTreeContains(
                        reg.fields,
                        fieldId)) {
                    return &reg;
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<std::uint64_t>
fieldAbsoluteLsb(
    const std::vector<Field>& fields,
    std::string_view fieldId,
    std::uint64_t parentLsb = 0)
{
    for (const auto& field : fields) {
        if (field.lsb >
            std::numeric_limits<
                std::uint64_t>::max() -
                parentLsb) {
            continue;
        }
        const std::uint64_t absoluteLsb =
            parentLsb + field.lsb;
        if (field.id == fieldId) {
            return absoluteLsb;
        }
        const auto nested =
            fieldAbsoluteLsb(
                field.members,
                fieldId,
                absoluteLsb);
        if (nested) {
            return nested;
        }
    }
    return std::nullopt;
}

void clearFieldResetValues(
    Field& field)
{
    field.resetValue.reset();
    for (auto& member : field.members) {
        clearFieldResetValues(member);
    }
}

void refreshFieldResetValues(
    Field& field,
    const std::optional<UnsignedValue>&
        registerReset,
    std::uint64_t parentLsb)
{
    if (field.msb < field.lsb ||
        field.lsb >
            std::numeric_limits<
                std::uint64_t>::max() -
                parentLsb) {
        clearFieldResetValues(field);
        return;
    }
    const std::uint64_t absoluteLsb =
        parentLsb + field.lsb;
    const std::size_t width =
        field.width();
    const bool rangeValid = width > 0 && absoluteLsb <= 32 && width <= 32 - absoluteLsb;
    if (registerReset && rangeValid) {
        field.resetValue =
            registerReset->slice(
                static_cast<std::size_t>(
                    absoluteLsb),
                width);
    } else {
        field.resetValue.reset();
    }
    for (auto& member : field.members) {
        refreshFieldResetValues(
            member,
            registerReset,
            absoluteLsb);
    }
}

void refreshRegisterFieldResetValues(
    Register& reg)
{
    for (auto& field : reg.fields) {
        refreshFieldResetValues(
            field,
            reg.resetValue,
            0);
    }
}

void refreshWorkspaceFieldResetValues(
    Workspace& workspace)
{
    for (auto& page :
         workspace.addressSpaces) {
        for (auto& block : page.blocks) {
            for (auto& reg :
                 block.registers) {
                refreshRegisterFieldResetValues(
                    reg);
            }
        }
    }
}

[[nodiscard]] bool requireCreationName(
    const QJsonObject& value,
    QString& error)
{
    const QJsonValue name =
        value.value(
            QStringLiteral("name"));
    if (!name.isString() ||
        name.toString().isEmpty()) {
        error =
            QStringLiteral(
                "Creation value must contain a non-empty 'name' string.");
        return false;
    }
    return true;
}

[[nodiscard]] bool addPage(
    Workspace& workspace,
    const QString& id,
    const QString& parentId,
    const QJsonObject& value,
    QString& error)
{
    if (parentId !=
        fromUtf8(workspace.id)) {
        error =
            QStringLiteral(
                "A page parent_id must be the workspace stable ID '%1'.")
                .arg(
                    fromUtf8(
                        workspace.id));
        return false;
    }
    if (!hasOnlyKeys(
            value,
            {QStringLiteral("name"),
             QStringLiteral("base"),
             QStringLiteral(
                 "address_width"),
             QStringLiteral(
                 "description")},
            error) ||
        !requireCreationName(
            value, error)) {
        return false;
    }
    AddressSpace page;
    page.id =
        id.toUtf8().toStdString();
    for (auto iterator =
             value.constBegin();
         iterator != value.constEnd();
         ++iterator) {
        if (!setPageProperty(
                page,
                iterator.key(),
                iterator.value(),
                error)) {
            return false;
        }
    }
    workspace.addressSpaces.push_back(
        std::move(page));
    return true;
}

[[nodiscard]] bool addBlock(
    Workspace& workspace,
    const QString& id,
    const QString& parentId,
    const QJsonObject& value,
    QString& error)
{
    AddressSpace* page =
        findAddressSpace(
            workspace,
            parentId.toUtf8()
                .toStdString());
    if (page == nullptr) {
        error =
            QStringLiteral(
                "A block parent_id must identify an existing page.");
        return false;
    }
    if (!hasOnlyKeys(
            value,
            {QStringLiteral("name"),
             QStringLiteral("base"),
             QStringLiteral("size"),
             QStringLiteral(
                 "description")},
            error) ||
        !requireCreationName(
            value, error)) {
        return false;
    }
    RegisterBlock block;
    block.id =
        id.toUtf8().toStdString();
    const QJsonValue baseValue =
        value.value(
            QStringLiteral("base"));
    const bool automaticBase =
        baseValue.isString() &&
        baseValue.toString() ==
            QStringLiteral("auto");
    for (auto iterator =
             value.constBegin();
         iterator != value.constEnd();
         ++iterator) {
        if (automaticBase &&
            iterator.key() ==
                QStringLiteral("base")) {
            continue;
        }
        if (!setBlockProperty(
                block,
                iterator.key(),
                iterator.value(),
                error)) {
            return false;
        }
    }
    if (automaticBase) {
        if (!block.size ||
            *block.size == 0) {
            error =
                QStringLiteral(
                    "Automatic Block base placement requires a positive explicit 'size'.");
            return false;
        }
        const auto placement =
            automaticBlockBase(
                *page, block);
        if (!placement) {
            error =
                QStringLiteral(
                    "Automatic Block base placement found no free %1-byte range for Block '%2' in Page '%3'. Specify an explicit base, move or resize an existing Block, increase Address Width, or reduce the requested Size.")
                    .arg(*block.size)
                    .arg(
                        fromUtf8(
                            block.name))
                    .arg(
                        fromUtf8(
                            page->name));
            return false;
        }
        block.baseAddress = *placement;
    }
    page->blocks.push_back(
        std::move(block));
    return true;
}

[[nodiscard]] bool addRegister(
    Workspace& workspace,
    const QString& id,
    const QString& parentId,
    const QJsonObject& value,
    QString& error)
{
    RegisterBlock* block =
        findRegisterBlock(
            workspace,
            parentId.toUtf8()
                .toStdString());
    if (block == nullptr) {
        error =
            QStringLiteral(
                "A register parent_id must identify an existing block.");
        return false;
    }
    if (!hasOnlyKeys(
            value,
            {QStringLiteral("name"),
             QStringLiteral("offset"),
             QStringLiteral("fixed"),
             QStringLiteral("width"),
             QStringLiteral("type"),
             QStringLiteral("minimum"),
             QStringLiteral("maximum"),
             QStringLiteral("initial"),
             QStringLiteral("reset"),
             QStringLiteral("access"),
             QStringLiteral("reserved"),
             QStringLiteral("tags"),
             QStringLiteral(
                 "description")},
            error) ||
        !requireCreationName(
            value, error)) {
        return false;
    }
    Register reg;
    reg.id =
        id.toUtf8().toStdString();
    reg.initialValue =
        UnsignedValue(0);
    reg.resetValue =
        UnsignedValue(0);
    const QJsonValue offsetValue =
        value.value(
            QStringLiteral("offset"));
    const bool automaticOffset =
        offsetValue.isString() &&
        offsetValue.toString() ==
            QStringLiteral("auto");
    for (auto iterator =
             value.constBegin();
         iterator != value.constEnd();
         ++iterator) {
        if (automaticOffset &&
            iterator.key() ==
                QStringLiteral(
                    "offset")) {
            continue;
        }
        if (!setRegisterProperty(
                reg,
                iterator.key(),
                iterator.value(),
                error)) {
            return false;
        }
    }
    if (automaticOffset) {
        const auto placement =
            automaticRegisterOffset(
                workspace,
                *block,
                reg);
        if (!placement) {
            error =
                QStringLiteral(
                    "Automatic Register offset placement found no aligned 4-byte range for the %1-bit Register '%2' in Block '%3'. Specify an explicit offset, free an address range, or increase the Block/Page capacity.")
                    .arg(reg.width)
                    .arg(
                        fromUtf8(
                            reg.name))
                    .arg(
                        fromUtf8(
                            block->name));
            return false;
        }
        reg.offset = *placement;
    }
    block->registers.push_back(
        std::move(reg));
    sortByOffsetAndId(
        block->registers);
    return true;
}

[[nodiscard]] bool addField(
    Workspace& workspace,
    const QString& id,
    const QString& parentId,
    const QJsonObject& value,
    QString& error)
{
    const std::string parentObjectId =
        parentId.toUtf8()
            .toStdString();
    Register* owner =
        findRegister(
            workspace,
            parentObjectId);
    Field* parentField =
        owner == nullptr
        ? findField(
              workspace,
              parentObjectId)
        : nullptr;
    if (owner == nullptr &&
        parentField != nullptr) {
        owner =
            fieldOwnerRegister(
                workspace,
                parentObjectId);
    }
    if (owner == nullptr ||
        (parentField == nullptr &&
         (owner->reserved ||
          owner->type !=
              FieldType::structure)) ||
        (parentField != nullptr &&
         parentField->type !=
             FieldType::structure)) {
        error =
            QStringLiteral(
                "A field parent_id must identify a structure register or compound field.");
        return false;
    }
    if (value.contains(
            QStringLiteral("reset"))) {
        error = QStringLiteral(
            "Field Reset is derived from its containing Register Reset and cannot be supplied when adding a Field. Set 'reset' on the parent Register instead.");
        return false;
    }
    if (!hasOnlyKeys(
            value,
            {QStringLiteral("name"),
             QStringLiteral("lsb"),
             QStringLiteral("msb"),
             QStringLiteral("width"),
             QStringLiteral("type"),
             QStringLiteral(
                 "software_access"),
             QStringLiteral(
                 "hardware_access"),
             QStringLiteral(
                 "read_side_effect"),
             QStringLiteral(
                 "write_side_effect"),
             QStringLiteral("minimum"),
             QStringLiteral("maximum"),
             QStringLiteral(
                 "description")},
            error) ||
        !requireCreationName(
            value, error)) {
        return false;
    }
    if (!value.contains(
            QStringLiteral("lsb"))) {
        error =
            QStringLiteral(
                "Field creation requires an explicit 'lsb'.");
        return false;
    }
    if (value.contains(
            QStringLiteral("msb")) &&
        value.contains(
            QStringLiteral("width"))) {
        error =
            QStringLiteral(
                "Field creation accepts either 'msb' or 'width', not both.");
        return false;
    }
    const QJsonValue lsbValue =
        value.value(
            QStringLiteral("lsb"));
    const bool automaticLsb =
        lsbValue.isString() &&
        lsbValue.toString() ==
            QStringLiteral("auto");
    if (automaticLsb &&
        value.contains(
            QStringLiteral("msb"))) {
        error =
            QStringLiteral(
                "Automatic Field placement accepts 'width', not an explicit 'msb'.");
        return false;
    }

    Field field;
    field.id =
        id.toUtf8().toStdString();
    if (!automaticLsb) {
        if (!requireUInt32(
                lsbValue,
                field.lsb,
                error)) {
            error.prepend(
                QStringLiteral(
                    "Invalid Field lsb. "));
            return false;
        }
    }
    field.msb = field.lsb;
    field.softwareAccess =
        owner->access;
    field.writeSideEffect =
        owner->access ==
                    AccessMode::writeOnly ||
            owner->access ==
                    AccessMode::readWrite
        ? WriteSideEffect::write
        : WriteSideEffect::none;
    const bool explicitWriteEffect =
        value.contains(
            QStringLiteral(
                "write_side_effect"));
    for (auto iterator =
             value.constBegin();
         iterator != value.constEnd();
         ++iterator) {
        if (iterator.key() ==
            QStringLiteral("lsb")) {
            continue;
        }
        if (!setFieldProperty(
                field,
                iterator.key(),
                iterator.value(),
                error)) {
            return false;
        }
    }
    if (!explicitWriteEffect) {
        field.writeSideEffect =
            field.softwareAccess ==
                        AccessMode::writeOnly ||
                field.softwareAccess ==
                        AccessMode::readWrite
            ? WriteSideEffect::write
            : WriteSideEffect::none;
    }
    if (automaticLsb) {
        const std::uint64_t width =
            field.width();
        const std::vector<Field>&
            siblings =
                parentField != nullptr
            ? parentField->members
            : owner->fields;
        const std::uint64_t
            containerWidth =
                parentField != nullptr
            ? parentField->width()
            : owner->width;
        const auto placement =
            automaticFieldLsb(
                siblings,
                width,
                containerWidth);
        if (!placement) {
            error =
                QStringLiteral(
                    "Automatic Field placement found no contiguous %1-bit range for Field '%2' in %3 '%4'. Specify an explicit lsb, move or narrow existing Fields, or increase the containing width.")
                    .arg(width)
                    .arg(
                        fromUtf8(
                            field.name))
                    .arg(
                        parentField !=
                                nullptr
                            ? QStringLiteral(
                                  "compound Field")
                            : QStringLiteral(
                                  "Register"))
                    .arg(
                        fromUtf8(
                            parentField !=
                                    nullptr
                                ? parentField
                                      ->name
                                : owner->name));
            return false;
        }
        field.lsb = *placement;
        field.msb =
            static_cast<std::uint32_t>(
                static_cast<
                    std::uint64_t>(
                    *placement) +
                width - 1);
    }
    std::uint64_t parentLsb = 0;
    if (parentField != nullptr) {
        const auto absolute =
            fieldAbsoluteLsb(
                owner->fields,
                parentField->id);
        if (!absolute) {
            error = QStringLiteral(
                "The destination compound Field has invalid geometry; repair it before adding a Field.");
            return false;
        }
        parentLsb = *absolute;
    }
    refreshFieldResetValues(
        field,
        owner->resetValue,
        parentLsb);
    if (parentField != nullptr) {
        parentField->members.push_back(
            std::move(field));
        sortFieldsByBit(
            parentField->members);
    } else {
        owner->fields.push_back(
            std::move(field));
        sortFieldsByBit(
            owner->fields);
    }
    return true;
}

[[nodiscard]] bool addEnum(
    Workspace& workspace,
    const QString& id,
    const QString& parentId,
    const QJsonObject& value,
    QString& error)
{
    const std::string parentObjectId =
        parentId.toUtf8()
            .toStdString();
    Register* reg =
        findRegister(
            workspace,
            parentObjectId);
    Field* field =
        reg == nullptr
        ? findField(
              workspace,
              parentObjectId)
        : nullptr;
    const FieldType type =
        field != nullptr
        ? field->type
        : reg != nullptr
            ? reg->type
            : FieldType::bits;
    if ((reg == nullptr &&
         field == nullptr) ||
        (type !=
             FieldType::enumeration &&
         type !=
             FieldType::boolean)) {
        error =
            QStringLiteral(
                "An enum parent_id must identify a bool or enum register/field.");
        return false;
    }
    if (!hasOnlyKeys(
            value,
            {QStringLiteral("name"),
             QStringLiteral("value"),
             QStringLiteral(
                 "description")},
            error) ||
        !requireCreationName(
            value, error) ||
        !value.contains(
            QStringLiteral("value"))) {
        if (error.isEmpty()) {
            error =
                QStringLiteral(
                    "Enum creation requires an explicit 'value'.");
        }
        return false;
    }
    EnumValue enumValue;
    enumValue.id =
        id.toUtf8().toStdString();
    for (auto iterator =
             value.constBegin();
         iterator != value.constEnd();
         ++iterator) {
        if (!setEnumProperty(
                enumValue,
                iterator.key(),
                iterator.value(),
                error)) {
            return false;
        }
    }
    if (field != nullptr) {
        field->enumValues.push_back(
            std::move(enumValue));
    } else {
        reg->enumValues.push_back(
            std::move(enumValue));
    }
    return true;
}

[[nodiscard]] bool addObject(
    Workspace& workspace,
    const QString& kind,
    const QString& id,
    const QString& parentId,
    const QJsonObject& value,
    QString& error)
{
    if (id.isEmpty()) {
        error =
            QStringLiteral(
                "Add operation 'id' must be a non-empty stable ID.");
        return false;
    }
    if (!findObjectJson(
             workspace,
             id.toUtf8()
                 .toStdString())
             .isUndefined()) {
        error =
            QStringLiteral(
                "Stable ID '%1' already exists.")
                .arg(id);
        return false;
    }
    if (parentId.isEmpty()) {
        error =
            QStringLiteral(
                "Add operation 'parent_id' must be a non-empty stable ID.");
        return false;
    }
    if (kind ==
        QStringLiteral("page")) {
        return addPage(
            workspace,
            id,
            parentId,
            value,
            error);
    }
    if (kind ==
        QStringLiteral("block")) {
        return addBlock(
            workspace,
            id,
            parentId,
            value,
            error);
    }
    if (kind ==
        QStringLiteral("register")) {
        return addRegister(
            workspace,
            id,
            parentId,
            value,
            error);
    }
    if (kind ==
        QStringLiteral("field")) {
        return addField(
            workspace,
            id,
            parentId,
            value,
            error);
    }
    if (kind ==
        QStringLiteral("enum")) {
        return addEnum(
            workspace,
            id,
            parentId,
            value,
            error);
    }
    error =
        QStringLiteral(
            "Add operation kind must be one of: page, block, register, field, enum.");
    return false;
}

template <typename Value>
[[nodiscard]] bool insertMovedObject(
    std::vector<Value>& siblings,
    Value moved,
    const std::optional<QString>&
        beforeId,
    const QString& kind,
    QString& error)
{
    if (!beforeId ||
        beforeId->isEmpty()) {
        siblings.push_back(
            std::move(moved));
        return true;
    }
    const auto insertion =
        std::ranges::find_if(
            siblings,
            [&beforeId](
                const Value& candidate) {
                return fromUtf8(
                           candidate.id) ==
                    *beforeId;
            });
    if (insertion ==
        siblings.end()) {
        error =
            QStringLiteral(
                "Move before_id '%1' must identify a %2 sibling in the destination parent.")
                .arg(
                    *beforeId,
                    kind);
        return false;
    }
    siblings.insert(
        insertion,
        std::move(moved));
    return true;
}

[[nodiscard]] bool moveObject(
    Workspace& workspace,
    const QString& id,
    const QString& parentId,
    const std::optional<QString>&
        beforeId,
    bool automaticPlacement,
    QString& kind,
    QString& previousParentId,
    bool& changed,
    QString& error)
{
    const auto descriptor =
        describeObject(
            workspace, id);
    if (!descriptor) {
        error =
            QStringLiteral(
                "No workspace object has stable ID '%1'.")
                .arg(id);
        return false;
    }
    kind = descriptor->kind;
    previousParentId =
        descriptor->parentId;
    changed = false;

    if (kind ==
        QStringLiteral(
            "workspace")) {
        error =
            QStringLiteral(
                "The workspace root cannot be moved.");
        return false;
    }
    if (automaticPlacement &&
        beforeId &&
        *beforeId == id) {
        error =
            QStringLiteral(
                "An automatically placed move cannot use the moved object itself as before_id.");
        return false;
    }
    if (!automaticPlacement &&
        parentId ==
            previousParentId &&
        !beforeId) {
        return true;
    }
    if (!automaticPlacement &&
        parentId ==
            previousParentId &&
        beforeId &&
        *beforeId == id) {
        return true;
    }

    const std::string objectId =
        id.toUtf8().toStdString();
    const std::string targetId =
        parentId.toUtf8()
            .toStdString();

    if (kind ==
        QStringLiteral("page")) {
        if (automaticPlacement) {
            error =
                QStringLiteral(
                    "Automatic move placement is supported only for block, register, and field objects.");
            return false;
        }
        if (parentId !=
            fromUtf8(workspace.id)) {
            error =
                QStringLiteral(
                    "A page parent_id must be the workspace stable ID '%1'.")
                    .arg(
                        fromUtf8(
                            workspace.id));
            return false;
        }
        const AddressSpace* source =
            findAddressSpace(
                workspace, objectId);
        if (source == nullptr) {
            error =
                QStringLiteral(
                    "Page '%1' could not be found.")
                    .arg(id);
            return false;
        }
        AddressSpace moved = *source;
        if (!removeObject(
                workspace,
                objectId)) {
            error =
                QStringLiteral(
                    "Page '%1' could not be detached from the workspace.")
                    .arg(id);
            return false;
        }
        if (!insertMovedObject(
                workspace.addressSpaces,
                std::move(moved),
                beforeId,
                QStringLiteral("page"),
                error)) {
            return false;
        }
        changed = true;
        return true;
    }

    if (kind ==
        QStringLiteral("block")) {
        const RegisterBlock* source =
            findRegisterBlock(
                workspace, objectId);
        if (source == nullptr ||
            findAddressSpace(
                workspace,
                targetId) == nullptr) {
            error =
                QStringLiteral(
                    "A block parent_id must identify an existing page.");
            return false;
        }
        RegisterBlock moved = *source;
        if (automaticPlacement &&
            (!moved.size ||
             *moved.size == 0)) {
            error =
                QStringLiteral(
                    "Automatic Block move placement requires a positive existing Size.");
            return false;
        }
        if (!removeObject(
                workspace,
                objectId)) {
            error =
                QStringLiteral(
                    "Block '%1' could not be detached from its current page.")
                    .arg(id);
            return false;
        }
        AddressSpace* target =
            findAddressSpace(
                workspace, targetId);
        if (target == nullptr) {
            error =
                QStringLiteral(
                    "Destination page '%1' disappeared while moving block '%2'.")
                    .arg(
                        parentId, id);
            return false;
        }
        if (automaticPlacement) {
            const auto placement =
                automaticBlockBase(
                    *target, moved);
            if (!placement) {
                error =
                    QStringLiteral(
                        "Automatic Block move placement found no free %1-byte range for Block '%2' in Page '%3'. Free or resize an existing Block, increase Address Width, or reduce the moved Block Size.")
                        .arg(*moved.size)
                        .arg(
                            fromUtf8(
                                moved.name))
                        .arg(
                            fromUtf8(
                                target->name));
                return false;
            }
            moved.baseAddress =
                *placement;
        }
        if (!insertMovedObject(
                target->blocks,
                std::move(moved),
                beforeId,
                QStringLiteral("block"),
                error)) {
            return false;
        }
        changed = true;
        return true;
    }

    if (kind ==
        QStringLiteral("register")) {
        if (beforeId) {
            if (automaticPlacement) {
                error =
                    QStringLiteral(
                        "Register before_id reordering cannot be combined with automatic placement.");
                return false;
            }
            if (parentId !=
                previousParentId) {
                error =
                    QStringLiteral(
                        "Register before_id reordering requires the existing parent block; use placement 'auto' when moving between blocks.");
                return false;
            }
            RegisterBlock* target =
                findRegisterBlock(
                    workspace,
                    targetId);
            if (target == nullptr) {
                error =
                    QStringLiteral(
                        "A register parent_id must identify its existing block for before_id reordering.");
                return false;
            }
            const auto source =
                std::ranges::find_if(
                    target->registers,
                    [&objectId](
                        const Register& reg) {
                        return reg.id ==
                            objectId;
                    });
            if (source ==
                target->registers.end()) {
                error =
                    QStringLiteral(
                        "Register '%1' could not be found in its existing block.")
                        .arg(id);
                return false;
            }
            if (source->addressFixed) {
                error =
                    QStringLiteral(
                        "Register '%1' has a fixed address and cannot be reordered. Make it movable first.")
                        .arg(id);
                return false;
            }

            std::vector<std::uint64_t>
                movableOffsets;
            for (const auto& reg :
                 target->registers) {
                if (!reg.addressFixed) {
                    movableOffsets.push_back(
                        reg.offset);
                }
            }
            std::ranges::sort(
                movableOffsets);

            Register moved = *source;
            std::vector<Register> reordered =
                target->registers;
            const auto detached =
                std::ranges::find_if(
                    reordered,
                    [&objectId](
                        const Register& reg) {
                        return reg.id ==
                            objectId;
                    });
            reordered.erase(detached);
            if (!insertMovedObject(
                    reordered,
                    std::move(moved),
                    beforeId,
                    QStringLiteral(
                        "register"),
                    error)) {
                return false;
            }
            std::size_t offsetIndex = 0;
            for (auto& reg : reordered) {
                if (!reg.addressFixed) {
                    reg.offset =
                        movableOffsets.at(
                            offsetIndex++);
                }
            }
            sortByOffsetAndId(
                reordered);
            target->registers =
                std::move(reordered);
            changed = true;
            return true;
        }
        const Register* source =
            findRegister(
                workspace, objectId);
        if (source == nullptr ||
            findRegisterBlock(
                workspace,
                targetId) == nullptr) {
            error =
                QStringLiteral(
                    "A register parent_id must identify an existing block.");
            return false;
        }
        Register moved = *source;
        if (!removeObject(
                workspace,
                objectId)) {
            error =
                QStringLiteral(
                    "Register '%1' could not be detached from its current block.")
                    .arg(id);
            return false;
        }
        RegisterBlock* target =
            findRegisterBlock(
                workspace, targetId);
        if (target == nullptr) {
            error =
                QStringLiteral(
                    "Destination block '%1' disappeared while moving register '%2'.")
                    .arg(
                        parentId, id);
            return false;
        }
        if (automaticPlacement) {
            const auto placement =
                automaticRegisterOffset(
                    workspace,
                    *target,
                    moved);
            if (!placement) {
                error =
                    QStringLiteral(
                        "Automatic Register move placement found no aligned 4-byte range for the %1-bit Register '%2' in Block '%3'. Free an address range or increase the Block/Page capacity.")
                        .arg(moved.width)
                        .arg(
                            fromUtf8(
                                moved.name))
                        .arg(
                            fromUtf8(
                                target->name));
                return false;
            }
            moved.offset = *placement;
        }
        refreshRegisterFieldResetValues(
            moved);
        target->registers.push_back(
            std::move(moved));
        sortByOffsetAndId(
            target->registers);
        changed = true;
        return true;
    }

    if (kind ==
        QStringLiteral("field")) {
        if (beforeId) {
            error =
                QStringLiteral(
                    "Move before_id is supported only for page and block objects; set Field lsb to change Field order.");
            return false;
        }
        const Field* source =
            findField(
                workspace, objectId);
        if (source == nullptr) {
            error =
                QStringLiteral(
                    "Field '%1' could not be found.")
                    .arg(id);
            return false;
        }
        if (source->id == targetId ||
            fieldTreeContains(
                source->members,
                targetId)) {
            error =
                QStringLiteral(
                    "Field '%1' cannot be moved under itself or one of its descendants.")
                    .arg(id);
            return false;
        }

        const Register* targetRegister =
            findRegister(
                workspace, targetId);
        const Field* targetField =
            targetRegister == nullptr
            ? findField(
                  workspace, targetId)
            : nullptr;
        if ((targetRegister == nullptr &&
             targetField == nullptr) ||
            (targetRegister != nullptr &&
             (targetRegister->reserved ||
              targetRegister->type !=
                  FieldType::structure)) ||
            (targetField != nullptr &&
             targetField->type !=
                 FieldType::structure)) {
            error =
                QStringLiteral(
                    "A field parent_id must identify a structure register or compound field.");
            return false;
        }

        Field moved = *source;
        if (!removeObject(
                workspace,
                objectId)) {
            error =
                QStringLiteral(
                    "Field '%1' could not be detached from its current parent.")
                    .arg(id);
            return false;
        }
        if (targetRegister != nullptr) {
            Register* target =
                findRegister(
                    workspace,
                    targetId);
            if (target == nullptr) {
                error =
                    QStringLiteral(
                        "Destination register '%1' disappeared while moving field '%2'.")
                        .arg(
                            parentId,
                            id);
                return false;
            }
            if (automaticPlacement) {
                const std::uint64_t width =
                    moved.width();
                const auto placement =
                    automaticFieldLsb(
                        target->fields,
                        width,
                        target->width);
                if (!placement) {
                    error =
                        QStringLiteral(
                            "Automatic Field move placement found no contiguous %1-bit range for Field '%2' in Register '%3'. Free or narrow existing Fields, or increase the Register width.")
                            .arg(width)
                            .arg(
                                fromUtf8(
                                    moved.name))
                            .arg(
                                fromUtf8(
                                    target->name));
                    return false;
                }
                moved.lsb = *placement;
                moved.msb =
                    static_cast<
                        std::uint32_t>(
                        static_cast<
                            std::uint64_t>(
                            *placement) +
                        width - 1);
            }
            refreshFieldResetValues(
                moved,
                target->resetValue,
                0);
            target->fields.push_back(
                std::move(moved));
            sortFieldsByBit(
                target->fields);
        } else {
            Field* target =
                findField(
                    workspace,
                    targetId);
            if (target == nullptr) {
                error =
                    QStringLiteral(
                        "Destination field '%1' disappeared while moving field '%2'.")
                        .arg(
                            parentId,
                            id);
                return false;
            }
            Register* owner =
                fieldOwnerRegister(
                    workspace,
                    targetId);
            const auto parentLsb =
                owner != nullptr
                ? fieldAbsoluteLsb(
                      owner->fields,
                      targetId)
                : std::nullopt;
            if (owner == nullptr ||
                !parentLsb) {
                error = QStringLiteral(
                    "The destination compound Field has invalid geometry; repair it before moving a Field.");
                return false;
            }
            if (automaticPlacement) {
                const std::uint64_t width =
                    moved.width();
                const auto placement =
                    automaticFieldLsb(
                        target->members,
                        width,
                        target->width());
                if (!placement) {
                    error =
                        QStringLiteral(
                            "Automatic Field move placement found no contiguous %1-bit range for Field '%2' in compound Field '%3'. Free or narrow existing Members, or increase the containing width.")
                            .arg(width)
                            .arg(
                                fromUtf8(
                                    moved.name))
                            .arg(
                                fromUtf8(
                                    target->name));
                    return false;
                }
                moved.lsb = *placement;
                moved.msb =
                    static_cast<
                        std::uint32_t>(
                        static_cast<
                            std::uint64_t>(
                            *placement) +
                        width - 1);
            }
            refreshFieldResetValues(
                moved,
                owner->resetValue,
                *parentLsb);
            target->members.push_back(
                std::move(moved));
            sortFieldsByBit(
                target->members);
        }
        changed = true;
        return true;
    }

    if (kind ==
        QStringLiteral("enum")) {
        if (automaticPlacement) {
            error =
                QStringLiteral(
                    "Automatic move placement is supported only for block, register, and field objects.");
            return false;
        }
        if (beforeId) {
            error =
                QStringLiteral(
                    "Move before_id is supported only for page and block objects.");
            return false;
        }
        const EnumValue* source =
            findEnumValue(
                workspace, objectId);
        Register* targetRegister =
            findRegister(
                workspace, targetId);
        Field* targetField =
            targetRegister == nullptr
            ? findField(
                  workspace, targetId)
            : nullptr;
        const FieldType targetType =
            targetField != nullptr
            ? targetField->type
            : targetRegister != nullptr
                ? targetRegister->type
                : FieldType::bits;
        if (source == nullptr ||
            (targetRegister == nullptr &&
             targetField == nullptr) ||
            (targetType !=
                 FieldType::enumeration &&
             targetType !=
                 FieldType::boolean)) {
            error =
                QStringLiteral(
                    "An enum parent_id must identify a bool or enum register/field.");
            return false;
        }

        EnumValue moved = *source;
        if (!removeObject(
                workspace,
                objectId)) {
            error =
                QStringLiteral(
                    "Enum value '%1' could not be detached from its current parent.")
                    .arg(id);
            return false;
        }
        if (targetRegister != nullptr) {
            targetRegister =
                findRegister(
                    workspace,
                    targetId);
            if (targetRegister ==
                nullptr) {
                error =
                    QStringLiteral(
                        "Destination register '%1' disappeared while moving enum '%2'.")
                        .arg(
                            parentId,
                            id);
                return false;
            }
            targetRegister
                ->enumValues
                .push_back(
                    std::move(moved));
        } else {
            targetField =
                findField(
                    workspace,
                    targetId);
            if (targetField == nullptr) {
                error =
                    QStringLiteral(
                        "Destination field '%1' disappeared while moving enum '%2'.")
                        .arg(
                            parentId,
                            id);
                return false;
            }
            targetField
                ->enumValues
                .push_back(
                    std::move(moved));
        }
        changed = true;
        return true;
    }

    error =
        QStringLiteral(
            "Move operation supports page, block, register, field, and enum objects.");
    return false;
}

[[nodiscard]] QString copiedDescendantId(
    const QString& rootId,
    std::string_view sourceId)
{
    return rootId +
        QStringLiteral("--") +
        fromUtf8(sourceId);
}

void collectObjectIds(
    const QJsonValue& value,
    std::vector<QString>& ids)
{
    if (value.isArray()) {
        for (const QJsonValue& member :
             value.toArray()) {
            collectObjectIds(
                member, ids);
        }
        return;
    }
    if (!value.isObject()) {
        return;
    }
    const QJsonObject object =
        value.toObject();
    const QJsonValue id =
        object.value(
            QStringLiteral("id"));
    if (id.isString() &&
        !id.toString().isEmpty()) {
        ids.push_back(
            id.toString());
    }
    for (auto iterator =
             object.constBegin();
         iterator != object.constEnd();
         ++iterator) {
        collectObjectIds(
            iterator.value(), ids);
    }
}

[[nodiscard]] std::optional<QString>
automaticCopyRootId(
    const Workspace& workspace,
    const QString& sourceId,
    QString& error)
{
    const QJsonValue source =
        findObjectJson(
            workspace,
            sourceId.toUtf8()
                .toStdString());
    if (!source.isObject()) {
        error =
            QStringLiteral(
                "No workspace object has stable ID '%1'.")
                .arg(sourceId);
        return std::nullopt;
    }

    std::vector<QString> sourceIds;
    collectObjectIds(
        source, sourceIds);
    if (std::ranges::find(
            sourceIds, sourceId) ==
        sourceIds.end()) {
        error =
            QStringLiteral(
                "Source object '%1' could not be inspected for automatic copy identity.")
                .arg(sourceId);
        return std::nullopt;
    }

    std::set<QString> existingIds;
    for (const auto& descriptor :
         describeObjects(workspace)) {
        existingIds.insert(
            descriptor.id);
    }
    const QString base =
        sourceId +
        QStringLiteral("-copy");
    for (std::uint64_t suffix = 1;; ++suffix) {
        const QString rootId =
            suffix == 1
            ? base
            : base +
                  QStringLiteral("-") +
                  QString::number(suffix);
        std::set<QString> assignedIds;
        bool available = true;
        for (const QString& objectId :
             sourceIds) {
            const QString assignedId =
                objectId == sourceId
                ? rootId
                : copiedDescendantId(
                      rootId,
                      objectId.toUtf8()
                          .toStdString());
            if (existingIds.contains(
                    assignedId) ||
                !assignedIds.insert(
                    assignedId)
                     .second) {
                available = false;
                break;
            }
        }
        if (available) {
            return rootId;
        }
        if (suffix ==
            std::numeric_limits<
                std::uint64_t>::max()) {
            error =
                QStringLiteral(
                    "No automatic stable ID remains available for copied object '%1'.")
                    .arg(sourceId);
            return std::nullopt;
        }
    }
}

[[nodiscard]] QString normalizedIdToken(
    const QString& value)
{
    QString result;
    bool previousSeparator = false;
    for (const QChar character : value) {
        const ushort code =
            character.unicode();
        if ((code >= 'a' &&
             code <= 'z') ||
            (code >= '0' &&
             code <= '9')) {
            result.append(
                character);
            previousSeparator = false;
        } else if (
            code >= 'A' &&
            code <= 'Z') {
            result.append(
                character.toLower());
            previousSeparator = false;
        } else if (
            !result.isEmpty() &&
            !previousSeparator) {
            result.append(
                QLatin1Char('-'));
            previousSeparator = true;
        }
    }
    while (result.endsWith(
        QLatin1Char('-'))) {
        result.chop(1);
    }
    return result.isEmpty()
        ? QStringLiteral("object")
        : result;
}

[[nodiscard]] QString automaticAddObjectId(
    const Workspace& workspace,
    const QString& kind,
    const QString& name)
{
    const QString base =
        kind +
        QStringLiteral("-") +
        normalizedIdToken(name);
    for (std::uint64_t suffix = 1;; ++suffix) {
        const QString candidate =
            suffix == 1
            ? base
            : base +
                  QStringLiteral("-") +
                  QString::number(suffix);
        if (findObjectJson(
                workspace,
                candidate.toUtf8()
                    .toStdString())
                .isUndefined()) {
            return candidate;
        }
    }
}

template <typename Value>
[[nodiscard]] std::string
automaticCopiedName(
    std::string_view original,
    const std::vector<Value>& siblings)
{
    const std::string base =
        original.empty()
        ? std::string{"Untitled"}
        : std::string{original};
    for (std::size_t suffix = 1;; ++suffix) {
        std::string candidate =
            base + " Copy";
        if (suffix > 1) {
            candidate +=
                " " +
                std::to_string(
                    suffix);
        }
        if (std::ranges::none_of(
                siblings,
                [&candidate](
                    const Value& sibling) {
                    return sibling.name ==
                        candidate;
                })) {
            return candidate;
        }
    }
}

[[nodiscard]] bool assignCopiedId(
    const Workspace& workspace,
    ObjectId& objectId,
    const QString& assignedId,
    std::set<QString>& assignedIds,
    QJsonObject& idMapping,
    QString& error)
{
    if (assignedId.isEmpty()) {
        error =
            QStringLiteral(
                "Copy operation generated an empty stable ID.");
        return false;
    }
    if (assignedIds.contains(
            assignedId) ||
        !findObjectJson(
             workspace,
             assignedId.toUtf8()
                 .toStdString())
             .isUndefined()) {
        error =
            QStringLiteral(
                "Copy stable ID '%1' already exists; choose a different new_id.")
                .arg(assignedId);
        return false;
    }
    const QString sourceId =
        fromUtf8(objectId);
    assignedIds.insert(
        assignedId);
    idMapping.insert(
        sourceId,
        assignedId);
    objectId =
        assignedId.toUtf8()
            .toStdString();
    return true;
}

[[nodiscard]] bool prepareCopiedEnum(
    const Workspace& workspace,
    EnumValue& value,
    const QString& assignedId,
    std::set<QString>& assignedIds,
    QJsonObject& idMapping,
    QString& error)
{
    if (!assignCopiedId(
            workspace,
            value.id,
            assignedId,
            assignedIds,
            idMapping,
            error)) {
        return false;
    }
    value.source = {};
    value.propertySources.clear();
    return true;
}

[[nodiscard]] bool prepareCopiedField(
    const Workspace& workspace,
    Field& field,
    const QString& assignedId,
    const QString& rootId,
    std::set<QString>& assignedIds,
    QJsonObject& idMapping,
    QString& error)
{
    if (!assignCopiedId(
            workspace,
            field.id,
            assignedId,
            assignedIds,
            idMapping,
            error)) {
        return false;
    }
    field.source = {};
    field.propertySources.clear();
    for (auto& enumValue :
         field.enumValues) {
        if (!prepareCopiedEnum(
                workspace,
                enumValue,
                copiedDescendantId(
                    rootId,
                    enumValue.id),
                assignedIds,
                idMapping,
                error)) {
            return false;
        }
    }
    for (auto& member :
         field.members) {
        if (!prepareCopiedField(
                workspace,
                member,
                copiedDescendantId(
                    rootId,
                    member.id),
                rootId,
                assignedIds,
                idMapping,
                error)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool prepareCopiedRegister(
    const Workspace& workspace,
    Register& reg,
    const QString& assignedId,
    const QString& rootId,
    std::set<QString>& assignedIds,
    QJsonObject& idMapping,
    QString& error)
{
    if (!assignCopiedId(
            workspace,
            reg.id,
            assignedId,
            assignedIds,
            idMapping,
            error)) {
        return false;
    }
    reg.source = {};
    reg.propertySources.clear();
    for (auto& enumValue :
         reg.enumValues) {
        if (!prepareCopiedEnum(
                workspace,
                enumValue,
                copiedDescendantId(
                    rootId,
                    enumValue.id),
                assignedIds,
                idMapping,
                error)) {
            return false;
        }
    }
    for (auto& field : reg.fields) {
        if (!prepareCopiedField(
                workspace,
                field,
                copiedDescendantId(
                    rootId,
                    field.id),
                rootId,
                assignedIds,
                idMapping,
                error)) {
            return false;
        }
    }
    refreshRegisterFieldResetValues(reg);
    return true;
}

[[nodiscard]] bool prepareCopiedBlock(
    const Workspace& workspace,
    RegisterBlock& block,
    const QString& assignedId,
    const QString& rootId,
    std::set<QString>& assignedIds,
    QJsonObject& idMapping,
    QString& error)
{
    if (!assignCopiedId(
            workspace,
            block.id,
            assignedId,
            assignedIds,
            idMapping,
            error)) {
        return false;
    }
    block.source = {};
    block.propertySources.clear();
    for (auto& reg :
         block.registers) {
        if (!prepareCopiedRegister(
                workspace,
                reg,
                copiedDescendantId(
                    rootId,
                    reg.id),
                rootId,
                assignedIds,
                idMapping,
                error)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool prepareCopiedPage(
    const Workspace& workspace,
    AddressSpace& page,
    const QString& assignedId,
    const QString& rootId,
    std::set<QString>& assignedIds,
    QJsonObject& idMapping,
    QString& error)
{
    if (!assignCopiedId(
            workspace,
            page.id,
            assignedId,
            assignedIds,
            idMapping,
            error)) {
        return false;
    }
    page.source = {};
    page.propertySources.clear();
    for (auto& block : page.blocks) {
        if (!prepareCopiedBlock(
                workspace,
                block,
                copiedDescendantId(
                    rootId,
                    block.id),
                rootId,
                assignedIds,
                idMapping,
                error)) {
            return false;
        }
    }
    return true;
}

template <typename Value,
          typename Setter>
[[nodiscard]] bool applyCopyOverrides(
    Value& copied,
    const QJsonObject& overrides,
    Setter setter,
    QString& error)
{
    for (auto iterator =
             overrides.constBegin();
         iterator !=
         overrides.constEnd();
         ++iterator) {
        if (!setter(
                copied,
                iterator.key(),
                iterator.value(),
                error)) {
            error.prepend(
                QStringLiteral(
                    "Invalid copy value override. "));
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool copyObject(
    Workspace& workspace,
    const QString& sourceId,
    const QString& newId,
    const QString& parentId,
    const QJsonObject& overrides,
    bool uniqueName,
    QString& kind,
    QJsonObject& idMapping,
    QString& error)
{
    const auto descriptor =
        describeObject(
            workspace, sourceId);
    if (!descriptor) {
        error =
            QStringLiteral(
                "No workspace object has stable ID '%1'.")
                .arg(sourceId);
        return false;
    }
    kind = descriptor->kind;
    if (kind ==
        QStringLiteral(
            "workspace")) {
        error =
            QStringLiteral(
                "The workspace root cannot be copied.");
        return false;
    }
    if (newId.isEmpty()) {
        error =
            QStringLiteral(
                "Copy operation 'new_id' must be a non-empty stable ID.");
        return false;
    }
    if (parentId.isEmpty()) {
        error =
            QStringLiteral(
                "Copy operation 'parent_id' must be a non-empty stable ID.");
        return false;
    }
    if (uniqueName &&
        overrides.contains(
            QStringLiteral("name"))) {
        error =
            QStringLiteral(
                "Copy operation 'unique_name' cannot be combined with a root 'name' override.");
        return false;
    }

    const std::string objectId =
        sourceId.toUtf8()
            .toStdString();
    const std::string targetId =
        parentId.toUtf8()
            .toStdString();
    std::set<QString> assignedIds;

    if (kind ==
        QStringLiteral("page")) {
        const AddressSpace* source =
            findAddressSpace(
                workspace, objectId);
        if (source == nullptr ||
            parentId !=
                fromUtf8(
                    workspace.id)) {
            error =
                QStringLiteral(
                    "A copied page parent_id must be the workspace stable ID '%1'.")
                    .arg(
                        fromUtf8(
                            workspace.id));
            return false;
        }
        AddressSpace copied = *source;
        if (!prepareCopiedPage(
                workspace,
                copied,
                newId,
                newId,
                assignedIds,
                idMapping,
                error) ||
            !applyCopyOverrides(
                copied,
                overrides,
                setPageProperty,
                error)) {
            return false;
        }
        if (uniqueName) {
            copied.name =
                automaticCopiedName(
                    source->name,
                    workspace
                        .addressSpaces);
        }
        workspace.addressSpaces
            .push_back(
                std::move(copied));
        return true;
    }

    if (kind ==
        QStringLiteral("block")) {
        const RegisterBlock* source =
            findRegisterBlock(
                workspace, objectId);
        AddressSpace* target =
            findAddressSpace(
                workspace, targetId);
        if (source == nullptr ||
            target == nullptr) {
            error =
                QStringLiteral(
                    "A copied block parent_id must identify an existing page.");
            return false;
        }
        RegisterBlock copied = *source;
        QJsonObject explicitOverrides =
            overrides;
        const bool automaticBase =
            explicitOverrides.value(
                QStringLiteral("base"))
                    .isString() &&
            explicitOverrides.value(
                QStringLiteral("base"))
                    .toString() ==
                QStringLiteral("auto");
        if (automaticBase) {
            explicitOverrides.remove(
                QStringLiteral("base"));
        }
        if (!prepareCopiedBlock(
                workspace,
                copied,
                newId,
                newId,
                assignedIds,
                idMapping,
                error) ||
            !applyCopyOverrides(
                copied,
                explicitOverrides,
                setBlockProperty,
                error)) {
            return false;
        }
        if (uniqueName) {
            copied.name =
                automaticCopiedName(
                    source->name,
                    target->blocks);
        }
        if (automaticBase) {
            if (!copied.size ||
                *copied.size == 0) {
                error =
                    QStringLiteral(
                        "Automatic Block base placement requires a positive explicit or copied 'size'.");
                return false;
            }
            const auto placement =
                automaticBlockBase(
                    *target, copied);
            if (!placement) {
                error =
                    QStringLiteral(
                        "Automatic Block base placement found no free %1-byte range for copied Block '%2' in Page '%3'. Specify an explicit base, move or resize an existing Block, increase Address Width, or reduce the copied Size.")
                        .arg(*copied.size)
                        .arg(
                            fromUtf8(
                                copied.name))
                        .arg(
                            fromUtf8(
                                target->name));
                return false;
            }
            copied.baseAddress =
                *placement;
        }
        target->blocks.push_back(
            std::move(copied));
        return true;
    }

    if (kind ==
        QStringLiteral("register")) {
        const Register* source =
            findRegister(
                workspace, objectId);
        RegisterBlock* target =
            findRegisterBlock(
                workspace, targetId);
        if (source == nullptr ||
            target == nullptr) {
            error =
                QStringLiteral(
                    "A copied register parent_id must identify an existing block.");
            return false;
        }
        Register copied = *source;
        QJsonObject explicitOverrides =
            overrides;
        const bool automaticOffset =
            explicitOverrides.value(
                QStringLiteral("offset"))
                    .isString() &&
            explicitOverrides.value(
                QStringLiteral("offset"))
                    .toString() ==
                QStringLiteral("auto");
        if (automaticOffset) {
            explicitOverrides.remove(
                QStringLiteral("offset"));
        }
        if (!prepareCopiedRegister(
                workspace,
                copied,
                newId,
                newId,
                assignedIds,
                idMapping,
                error) ||
            !applyCopyOverrides(
                copied,
                explicitOverrides,
                setRegisterProperty,
                error)) {
            return false;
        }
        if (uniqueName) {
            copied.name =
                automaticCopiedName(
                    source->name,
                    target->registers);
        }
        if (automaticOffset) {
            const auto placement =
                automaticRegisterOffset(
                    workspace,
                    *target,
                    copied);
            if (!placement) {
                error =
                    QStringLiteral(
                        "Automatic Register offset placement found no aligned 4-byte range for copied %1-bit Register '%2' in Block '%3'. Specify an explicit offset, free an address range, or increase the Block/Page capacity.")
                        .arg(copied.width)
                        .arg(
                            fromUtf8(
                                copied.name))
                        .arg(
                            fromUtf8(
                                target->name));
                return false;
            }
            copied.offset = *placement;
        }
        refreshRegisterFieldResetValues(
            copied);
        target->registers.push_back(
            std::move(copied));
        sortByOffsetAndId(
            target->registers);
        return true;
    }

    if (kind ==
        QStringLiteral("field")) {
        const Field* source =
            findField(
                workspace, objectId);
        Register* targetRegister =
            findRegister(
                workspace, targetId);
        Field* targetField =
            targetRegister == nullptr
            ? findField(
                  workspace, targetId)
            : nullptr;
        Register* destinationRegister =
            targetRegister != nullptr
            ? targetRegister
            : fieldOwnerRegister(
                  workspace, targetId);
        if (source == nullptr ||
            (targetRegister == nullptr &&
             targetField == nullptr) ||
            destinationRegister == nullptr ||
            (targetRegister != nullptr &&
             (targetRegister->reserved ||
              targetRegister->type !=
                  FieldType::structure)) ||
            (targetField != nullptr &&
             targetField->type !=
                 FieldType::structure)) {
            error =
                QStringLiteral(
                    "A copied field parent_id must identify a structure register or compound field.");
            return false;
        }
        Field copied = *source;
        QJsonObject explicitOverrides =
            overrides;
        const bool automaticLsb =
            explicitOverrides.value(
                QStringLiteral("lsb"))
                    .isString() &&
            explicitOverrides.value(
                QStringLiteral("lsb"))
                    .toString() ==
                QStringLiteral("auto");
        if (automaticLsb &&
            explicitOverrides.contains(
                QStringLiteral("msb"))) {
            error =
                QStringLiteral(
                    "Automatic copied Field placement accepts 'width', not an explicit 'msb'.");
            return false;
        }
        if (automaticLsb) {
            explicitOverrides.remove(
                QStringLiteral("lsb"));
        }
        if (!prepareCopiedField(
                workspace,
                copied,
                newId,
                newId,
                assignedIds,
                idMapping,
                error) ||
            !applyCopyOverrides(
                copied,
                explicitOverrides,
                setFieldProperty,
                error)) {
            return false;
        }
        if (uniqueName) {
            copied.name =
                targetField != nullptr
                ? automaticCopiedName(
                      source->name,
                      targetField
                          ->members)
                : automaticCopiedName(
                      source->name,
                      targetRegister
                          ->fields);
        }
        if (automaticLsb) {
            const std::uint64_t width =
                copied.width();
            const std::vector<Field>&
                siblings =
                    targetField != nullptr
                ? targetField->members
                : targetRegister->fields;
            const std::uint64_t
                containerWidth =
                    targetField != nullptr
                ? targetField->width()
                : targetRegister->width;
            const auto placement =
                automaticFieldLsb(
                    siblings,
                    width,
                    containerWidth);
            if (!placement) {
                error =
                    QStringLiteral(
                        "Automatic Field placement found no contiguous %1-bit range for copied Field '%2' in %3 '%4'. Specify an explicit lsb, move or narrow existing Fields, or increase the containing width.")
                        .arg(width)
                        .arg(
                            fromUtf8(
                                copied.name))
                        .arg(
                            targetField !=
                                    nullptr
                                ? QStringLiteral(
                                      "compound Field")
                                : QStringLiteral(
                                      "Register"))
                        .arg(
                            fromUtf8(
                                targetField !=
                                        nullptr
                                    ? targetField
                                          ->name
                                    : targetRegister
                                          ->name));
                return false;
            }
            copied.lsb = *placement;
            copied.msb =
                static_cast<
                    std::uint32_t>(
                    static_cast<
                        std::uint64_t>(
                        *placement) +
                    width - 1);

        }
        std::uint64_t parentLsb = 0;
        if (targetField != nullptr) {
            const auto absolute =
                fieldAbsoluteLsb(
                    destinationRegister
                        ->fields,
                    targetField->id);
            if (!absolute) {
                error = QStringLiteral(
                    "The destination compound Field has invalid geometry; repair it before copying a Field.");
                return false;
            }
            parentLsb = *absolute;
        }
        refreshFieldResetValues(
            copied,
            destinationRegister
                ->resetValue,
            parentLsb);
        if (targetRegister != nullptr) {
            targetRegister->fields
                .push_back(
                    std::move(copied));
            sortFieldsByBit(
                targetRegister
                    ->fields);
        } else {
            targetField->members
                .push_back(
                    std::move(copied));
            sortFieldsByBit(
                targetField
                    ->members);
        }
        return true;
    }

    if (kind ==
        QStringLiteral("enum")) {
        const EnumValue* source =
            findEnumValue(
                workspace, objectId);
        Register* targetRegister =
            findRegister(
                workspace, targetId);
        Field* targetField =
            targetRegister == nullptr
            ? findField(
                  workspace, targetId)
            : nullptr;
        const FieldType targetType =
            targetField != nullptr
            ? targetField->type
            : targetRegister != nullptr
                ? targetRegister->type
                : FieldType::bits;
        if (source == nullptr ||
            (targetRegister == nullptr &&
             targetField == nullptr) ||
            (targetType !=
                 FieldType::enumeration &&
             targetType !=
                 FieldType::boolean)) {
            error =
                QStringLiteral(
                    "A copied enum parent_id must identify a bool or enum register/field.");
            return false;
        }
        EnumValue copied = *source;
        if (!prepareCopiedEnum(
                workspace,
                copied,
                newId,
                assignedIds,
                idMapping,
                error) ||
            !applyCopyOverrides(
                copied,
                overrides,
                setEnumProperty,
                error)) {
            return false;
        }
        if (uniqueName) {
            copied.name =
                targetRegister != nullptr
                ? automaticCopiedName(
                      source->name,
                      targetRegister
                          ->enumValues)
                : automaticCopiedName(
                      source->name,
                      targetField
                          ->enumValues);
        }
        if (targetRegister != nullptr) {
            targetRegister->enumValues
                .push_back(
                    std::move(copied));
        } else {
            targetField->enumValues
                .push_back(
                    std::move(copied));
        }
        return true;
    }

    error =
        QStringLiteral(
            "Copy operation supports page, block, register, field, and enum objects.");
    return false;
}

[[nodiscard]] std::size_t
fieldDescendantCount(
    const Field& field)
{
    std::size_t result =
        field.enumValues.size();
    for (const auto& member :
         field.members) {
        result +=
            1 +
            fieldDescendantCount(
                member);
    }
    return result;
}

[[nodiscard]] std::size_t
registerDescendantCount(
    const Register& reg)
{
    std::size_t result =
        reg.enumValues.size();
    for (const auto& field :
         reg.fields) {
        result +=
            1 +
            fieldDescendantCount(
                field);
    }
    return result;
}

[[nodiscard]] std::size_t
blockDescendantCount(
    const RegisterBlock& block)
{
    std::size_t result = 0;
    for (const auto& reg :
         block.registers) {
        result +=
            1 +
            registerDescendantCount(
                reg);
    }
    return result;
}

[[nodiscard]] std::size_t
pageDescendantCount(
    const AddressSpace& page)
{
    std::size_t result = 0;
    for (const auto& block :
         page.blocks) {
        result +=
            1 +
            blockDescendantCount(
                block);
    }
    return result;
}

[[nodiscard]] std::size_t
descendantCount(
    const Workspace& workspace,
    const QString& kind,
    const std::string& id)
{
    if (kind ==
        QStringLiteral("page")) {
        const auto* page =
            findAddressSpace(
                workspace, id);
        return page == nullptr
            ? 0
            : pageDescendantCount(
                  *page);
    }
    if (kind ==
        QStringLiteral("block")) {
        const auto* block =
            findRegisterBlock(
                workspace, id);
        return block == nullptr
            ? 0
            : blockDescendantCount(
                  *block);
    }
    if (kind ==
        QStringLiteral("register")) {
        const auto* reg =
            findRegister(
                workspace, id);
        return reg == nullptr
            ? 0
            : registerDescendantCount(
                  *reg);
    }
    if (kind ==
        QStringLiteral("field")) {
        const auto* field =
            findField(
                workspace, id);
        return field == nullptr
            ? 0
            : fieldDescendantCount(
                  *field);
    }
    return 0;
}

[[nodiscard]] bool removeObjectChecked(
    Workspace& workspace,
    const QString& id,
    bool cascade,
    QString& kind,
    std::size_t& descendants,
    QString& error)
{
    const std::string objectId =
        id.toUtf8().toStdString();
    const QJsonValue object =
        findObjectJson(
            workspace,
            objectId);
    if (!object.isObject()) {
        error =
            QStringLiteral(
                "No workspace object has stable ID '%1'.")
                .arg(id);
        return false;
    }
    kind =
        object.toObject()
            .value(
                QStringLiteral("kind"))
            .toString();
    if (kind ==
        QStringLiteral("workspace")) {
        error =
            QStringLiteral(
                "The workspace root cannot be removed by apply.");
        return false;
    }
    descendants =
        descendantCount(
            workspace,
            kind,
            objectId);
    if (descendants > 0 &&
        !cascade) {
        error =
            QStringLiteral(
                "Removing %1 '%2' would also remove %3 descendant object(s); set 'cascade' to true explicitly.")
                .arg(
                    kind,
                    id)
                .arg(descendants);
        return false;
    }
    if (!removeObject(
            workspace,
            objectId)) {
        error =
            QStringLiteral(
                "Object '%1' could not be removed.")
                .arg(id);
        return false;
    }
    return true;
}

[[nodiscard]] Diagnostic objectNotFound(
    const QString& id,
    const QString& project)
{
    Diagnostic diagnostic;
    diagnostic.code =
        std::string(
            objectNotFoundCode);
    diagnostic.severity =
        DiagnosticSeverity::error;
    diagnostic.message =
        QStringLiteral(
            "No workspace object has stable ID '%1'.")
            .arg(id)
            .toUtf8()
            .toStdString();
    diagnostic.source.workbook =
        toPath(project);
    return diagnostic;
}

[[nodiscard]] Diagnostic requestedTargetUnavailable(
    GenerationTargetKind kind,
    const QString& project)
{
    Diagnostic diagnostic;
    diagnostic.code =
        std::string(
            requestedTargetUnavailableCode);
    diagnostic.severity =
        DiagnosticSeverity::error;
    diagnostic.message =
        QStringLiteral(
            "Generation target '%1' is not configured in this project.")
            .arg(
                generationTargetToken(kind))
            .toUtf8()
            .toStdString();
    diagnostic.source.workbook =
        toPath(project);
    return diagnostic;
}

[[nodiscard]] bool selectGenerationManifest(
    const ProjectManifest& manifest,
    const GenerationTargetSet& requested,
    const QString& project,
    ProjectManifest& selected,
    std::vector<Diagnostic>& diagnostics)
{
    GenerationTargetSet missing;
    selected = manifestForTargets(
        manifest,
        requested,
        &missing);
    for (const GenerationTargetKind kind : missing) {
        diagnostics.push_back(
            requestedTargetUnavailable(
                kind,
                project));
    }
    return missing.empty();
}

[[nodiscard]] QJsonObject projectSchemaJson()
{
    return {
        {QStringLiteral("current_version"),
         ProjectManifest::currentSchemaVersion},
        {QStringLiteral("supported_versions"),
         QJsonArray{
             ProjectManifest::
                 currentSchemaVersion}},
    };
}

[[nodiscard]] QJsonObject jsonTransportContract()
{
    return {
        {QStringLiteral("encoding"),
         QStringLiteral("utf-8")},
        {QStringLiteral("response_stream"),
         QStringLiteral("stdout")},
        {QStringLiteral("framing"),
         QStringLiteral(
             "single_compact_json_object_lf")},
        {QStringLiteral("stderr"),
         QStringLiteral(
             "empty_for_regmapc_responses")},
        {QStringLiteral(
             "diagnostics_location"),
         QStringLiteral(
             "diagnostics")},
        {QStringLiteral(
             "stdin_patch_token"),
         QStringLiteral("-")},
        {QStringLiteral(
             "stdin_patch_document"),
         QStringLiteral(
             "one_utf8_json_document")},
    };
}

[[nodiscard]] QJsonObject workbenchNavigation(
    const QString& project,
    const QString& stableId,
    const QString& kind,
    const QString& path)
{
    return {
        {QStringLiteral(
             "protocol_version"),
         1},
        {QStringLiteral("project"),
         project},
        {QStringLiteral("stable_id"),
         stableId},
        {QStringLiteral("kind"),
         kind},
        {QStringLiteral("path"),
         path},
        {QStringLiteral("arguments"),
         QJsonArray{
             QStringLiteral("--project"),
             project,
             QStringLiteral("--select"),
             stableId}},
    };
}

[[nodiscard]] QJsonObject schemaJson()
{
    const QJsonArray kinds{
        QStringLiteral("workspace"),
        QStringLiteral("page"),
        QStringLiteral("block"),
        QStringLiteral("register"),
        QStringLiteral("field"),
        QStringLiteral("enum"),
    };
    const QJsonArray commands{
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("help")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("none")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] help [command]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("version")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("none")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] version")},
            {QStringLiteral("aliases"),
             QJsonArray{
                 QStringLiteral(
                     "--version")}},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("schema")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("none")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] schema")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("init")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("create")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("always")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] init <project.regmap.yaml> [--name <workspace-name>] [--workspace-id <stable-id>] [--output-dir <relative-path>] [--target <target>]... [--xlsx-file <relative-path>] [--c-header-file <relative-path>] [--markdown-file <relative-path>] [--no-generate]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("summary")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] summary <project.regmap.yaml>")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("list")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] list <project.regmap.yaml> [--kind <kind>] [--parent <stable-id> [--recursive]] [--tag <tag>] [--offset <count>] [--limit <count> | --all] [--expect <sha256:...>]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("find")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] find <project.regmap.yaml> <query> [--exact] [--require-one] [--kind <kind>] [--parent <stable-id> [--recursive]] [--tag <tag>] [--offset <count>] [--limit <count> | --all] [--expect <sha256:...>]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("get")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] get <project.regmap.yaml> [stable-id] [--expect <sha256:...>]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("get-many")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] get-many <project.regmap.yaml> <stable-id>... [--expect <sha256:...>]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("validate")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] validate <project.regmap.yaml>")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("diff")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] diff <before.regmap.yaml> <after.regmap.yaml> [--kind <kind>] [--offset <count>] [--limit <count>] [--expect-before <sha256:...>] [--expect-after <sha256:...>] [--require-equal]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("status")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("read")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral("never")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] status <project.regmap.yaml> [--target <target>]... [--require-current]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("generate")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("write")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral(
                 "unless_dry_run")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] generate <project.regmap.yaml> [--target <target>]... [--dry-run] [--expect <sha256:...>]")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("apply")},
            {QStringLiteral(
                 "project_access"),
             QStringLiteral("write")},
            {QStringLiteral(
                 "file_write_behavior"),
             QStringLiteral(
                 "unless_dry_run")},
            {QStringLiteral("usage"),
             QStringLiteral(
                 "regmapc [--json] apply <project.regmap.yaml> <patch.json|-> [--dry-run] [--expect <sha256:...> | --force]")},
        },
    };
    const auto positional =
        [](const QString& name,
           const QString& valueType,
           bool required = true) {
            return QJsonObject{
                {QStringLiteral("name"),
                 name},
                {QStringLiteral(
                     "value_type"),
                 valueType},
                {QStringLiteral(
                     "required"),
                 required},
            };
        };
    const auto flag =
        [](const QString& name) {
            return QJsonObject{
                {QStringLiteral("name"),
                 name},
                {QStringLiteral("kind"),
                 QStringLiteral("flag")},
                {QStringLiteral(
                     "repeatable"),
                 false},
            };
        };
    const auto option =
        [](const QString& name,
           const QString& valueName,
           const QString& valueType,
           QJsonArray allowedValues =
               QJsonArray{},
           bool repeatable = false) {
            QJsonObject result{
                {QStringLiteral("name"),
                 name},
                {QStringLiteral("kind"),
                 QStringLiteral("value")},
                {QStringLiteral(
                     "value_name"),
                 valueName},
                {QStringLiteral(
                     "value_type"),
                 valueType},
                {QStringLiteral(
                     "repeatable"),
                 repeatable},
            };
            if (!allowedValues.isEmpty()) {
                result.insert(
                    QStringLiteral(
                        "allowed_values"),
                    std::move(
                        allowedValues));
            }
            return result;
        };
    const QJsonArray noArguments;
    const QJsonArray projectArgument{
        positional(
            QStringLiteral("project"),
            QStringLiteral("path"))};
    const QJsonArray kindValues{
        QStringLiteral("workspace"),
        QStringLiteral("page"),
        QStringLiteral("block"),
        QStringLiteral("register"),
        QStringLiteral("field"),
        QStringLiteral("enum"),
        QStringLiteral("all"),
    };
    const QJsonArray generationTargetValues{
        QStringLiteral("xlsx"),
        QStringLiteral("c-header"),
        QStringLiteral("markdown"),
    };
    const QJsonObject
        commandArgumentSchemas{
            {QStringLiteral("help"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "command"),
                          QStringLiteral(
                              "command_name"),
                          false)}},
                 {QStringLiteral("options"),
                  noArguments}}},
            {QStringLiteral("version"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  noArguments},
                 {QStringLiteral("options"),
                  noArguments}}},
            {QStringLiteral("schema"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  noArguments},
                 {QStringLiteral("options"),
                  noArguments}}},
            {QStringLiteral("init"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "project"),
                          QStringLiteral(
                              "path"))}},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--name"),
                          QStringLiteral(
                              "workspace-name"),
                          QStringLiteral(
                              "string")),
                      option(
                          QStringLiteral(
                              "--workspace-id"),
                          QStringLiteral(
                              "stable-id"),
                          QStringLiteral(
                              "stable_id")),
                      option(
                          QStringLiteral(
                              "--output-dir"),
                          QStringLiteral(
                              "relative-path"),
                          QStringLiteral(
                              "relative_path")),
                      option(
                          QStringLiteral(
                              "--target"),
                          QStringLiteral(
                              "target"),
                          QStringLiteral(
                              "enum"),
                          generationTargetValues,
                          true),
                      option(
                          QStringLiteral(
                              "--xlsx-file"),
                          QStringLiteral(
                              "relative-path"),
                          QStringLiteral(
                              "relative_path")),
                      option(
                          QStringLiteral(
                              "--c-header-file"),
                          QStringLiteral(
                              "relative-path"),
                          QStringLiteral(
                              "relative_path")),
                      option(
                          QStringLiteral(
                              "--markdown-file"),
                          QStringLiteral(
                              "relative-path"),
                          QStringLiteral(
                              "relative_path")),
                      flag(
                          QStringLiteral(
                              "--no-generate"))}},
                 {QStringLiteral(
                      "mutually_exclusive_options"),
                  QJsonArray{
                      QJsonArray{
                          QStringLiteral(
                              "--target"),
                          QStringLiteral(
                              "--no-generate")}}}}},
            {QStringLiteral("summary"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  projectArgument},
                 {QStringLiteral("options"),
                  noArguments}}},
            {QStringLiteral("list"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  projectArgument},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--kind"),
                          QStringLiteral(
                              "kind"),
                          QStringLiteral(
                              "enum"),
                          kindValues),
                      option(
                          QStringLiteral(
                              "--parent"),
                          QStringLiteral(
                              "stable-id"),
                          QStringLiteral(
                              "stable_id")),
                      flag(
                          QStringLiteral(
                              "--recursive")),
                      option(
                          QStringLiteral(
                              "--tag"),
                          QStringLiteral(
                              "tag"),
                          QStringLiteral(
                              "string")),
                      option(
                          QStringLiteral(
                              "--offset"),
                          QStringLiteral(
                              "count"),
                          QStringLiteral(
                              "non_negative_integer")),
                      option(
                          QStringLiteral(
                              "--limit"),
                          QStringLiteral(
                              "count"),
                          QStringLiteral(
                              "positive_integer")),
                      flag(
                          QStringLiteral(
                              "--all")),
                      option(
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "revision"))}},
                 {QStringLiteral(
                      "mutually_exclusive_options"),
                  QJsonArray{
                      QJsonArray{
                          QStringLiteral("--limit"),
                          QStringLiteral("--all")}}}}},
            {QStringLiteral("find"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "project"),
                          QStringLiteral(
                              "path")),
                      positional(
                          QStringLiteral(
                              "query"),
                          QStringLiteral(
                              "string"))}},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--kind"),
                          QStringLiteral(
                              "kind"),
                          QStringLiteral(
                              "enum"),
                          kindValues),
                      option(
                          QStringLiteral(
                              "--parent"),
                          QStringLiteral(
                              "stable-id"),
                          QStringLiteral(
                              "stable_id")),
                      flag(
                          QStringLiteral(
                              "--recursive")),
                      flag(
                          QStringLiteral(
                              "--exact")),
                      flag(
                          QStringLiteral(
                              "--require-one")),
                      option(
                          QStringLiteral(
                              "--tag"),
                          QStringLiteral(
                              "tag"),
                          QStringLiteral(
                              "string")),
                      option(
                          QStringLiteral(
                              "--offset"),
                          QStringLiteral(
                              "count"),
                          QStringLiteral(
                              "non_negative_integer")),
                      option(
                          QStringLiteral(
                              "--limit"),
                          QStringLiteral(
                              "count"),
                          QStringLiteral(
                              "positive_integer")),
                      flag(
                          QStringLiteral(
                              "--all")),
                      option(
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "revision"))}},
                 {QStringLiteral(
                      "mutually_exclusive_options"),
                  QJsonArray{
                      QJsonArray{
                          QStringLiteral("--limit"),
                          QStringLiteral("--all")}}}}},
            {QStringLiteral("get"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "project"),
                          QStringLiteral(
                              "path")),
                      positional(
                          QStringLiteral(
                              "stable-id"),
                          QStringLiteral(
                              "stable_id"),
                          false)}},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "revision"))}}}},
            {QStringLiteral("get-many"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "project"),
                          QStringLiteral(
                              "path")),
                      QJsonObject{
                          {QStringLiteral(
                               "name"),
                           QStringLiteral(
                               "stable-id")},
                          {QStringLiteral(
                               "value_type"),
                           QStringLiteral(
                               "stable_id")},
                          {QStringLiteral(
                               "required"),
                           true},
                          {QStringLiteral(
                               "repeatable"),
                           true},
                      }}},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "revision"))}}}},
            {QStringLiteral("validate"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  projectArgument},
                 {QStringLiteral("options"),
                  noArguments}}},
            {QStringLiteral("diff"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "before_project"),
                          QStringLiteral(
                              "path")),
                      positional(
                          QStringLiteral(
                              "after_project"),
                          QStringLiteral(
                              "path"))}},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--kind"),
                          QStringLiteral(
                              "kind"),
                          QStringLiteral(
                              "enum"),
                          kindValues),
                      option(
                          QStringLiteral(
                              "--offset"),
                          QStringLiteral(
                              "count"),
                          QStringLiteral(
                              "non_negative_integer")),
                      option(
                          QStringLiteral(
                              "--limit"),
                          QStringLiteral(
                              "count"),
                          QStringLiteral(
                              "positive_integer")),
                      option(
                          QStringLiteral(
                              "--expect-before"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "revision")),
                      option(
                          QStringLiteral(
                              "--expect-after"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "revision")),
                      flag(
                          QStringLiteral(
                              "--require-equal"))}}}},
            {QStringLiteral("status"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  projectArgument},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--target"),
                          QStringLiteral(
                              "target"),
                          QStringLiteral(
                              "enum"),
                          generationTargetValues,
                          true),
                      flag(
                          QStringLiteral(
                              "--require-current"))}}}},
            {QStringLiteral("generate"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  projectArgument},
                 {QStringLiteral("options"),
                  QJsonArray{
                      option(
                          QStringLiteral(
                              "--target"),
                          QStringLiteral(
                              "target"),
                          QStringLiteral(
                              "enum"),
                          generationTargetValues,
                          true),
                      flag(
                          QStringLiteral(
                              "--dry-run")),
                      option(
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "sha256_revision"))}}}},
            {QStringLiteral("apply"),
             QJsonObject{
                 {QStringLiteral(
                      "positionals"),
                  QJsonArray{
                      positional(
                          QStringLiteral(
                              "project"),
                          QStringLiteral(
                              "path")),
                      positional(
                          QStringLiteral(
                              "patch"),
                          QStringLiteral(
                              "path_or_stdin"))}},
                 {QStringLiteral("options"),
                  QJsonArray{
                      flag(
                          QStringLiteral(
                              "--dry-run")),
                      option(
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "revision"),
                          QStringLiteral(
                              "sha256_revision")),
                      flag(
                          QStringLiteral(
                              "--force"))}},
                 {QStringLiteral(
                      "mutually_exclusive_options"),
                  QJsonArray{
                      QJsonArray{
                          QStringLiteral(
                              "--expect"),
                          QStringLiteral(
                              "--force")}}},
                 {QStringLiteral(
                      "write_precondition"),
                  QStringLiteral(
                      "A real apply requires --expect or --force; a patch expected_revision satisfies --expect.")}}},
        };
    const QJsonArray globalOptions{
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("--json")},
            {QStringLiteral("kind"),
             QStringLiteral("flag")},
            {QStringLiteral("repeatable"),
             true},
            {QStringLiteral("position"),
             QStringLiteral("any")},
            {QStringLiteral("effect"),
             QStringLiteral(
                 "returns one compact API-versioned JSON object")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("--help")},
            {QStringLiteral("aliases"),
             QJsonArray{
                 QStringLiteral("-h")}},
            {QStringLiteral("kind"),
             QStringLiteral("flag")},
            {QStringLiteral("repeatable"),
             true},
            {QStringLiteral("position"),
             QStringLiteral("any")},
            {QStringLiteral("effect"),
             QStringLiteral(
                 "returns complete capability discovery without a command, or focused command help when a command is present")},
        },
        QJsonObject{
            {QStringLiteral("name"),
             QStringLiteral("--")},
            {QStringLiteral("kind"),
             QStringLiteral(
                 "terminator")},
            {QStringLiteral("repeatable"),
             false},
            {QStringLiteral("position"),
             QStringLiteral(
                 "before an option-like positional value")},
            {QStringLiteral("effect"),
             QStringLiteral(
                 "stops global option recognition; the next token is treated as positional data")},
        },
    };
    const QJsonObject writableProperties{
        {QStringLiteral("workspace"),
         QJsonArray{
             QStringLiteral("name")}},
        {QStringLiteral("page"),
         QJsonArray{
             QStringLiteral("name"),
             QStringLiteral("base"),
             QStringLiteral(
                 "address_width"),
             QStringLiteral(
                 "description")}},
        {QStringLiteral("block"),
         QJsonArray{
             QStringLiteral("name"),
             QStringLiteral("base"),
             QStringLiteral("size"),
             QStringLiteral(
                 "description")}},
        {QStringLiteral("register"),
         QJsonArray{
             QStringLiteral("name"),
             QStringLiteral("offset"),
             QStringLiteral("fixed"),
             QStringLiteral("width"),
             QStringLiteral("type"),
             QStringLiteral("minimum"),
             QStringLiteral("maximum"),
             QStringLiteral("initial"),
             QStringLiteral("reset"),
             QStringLiteral("access"),
             QStringLiteral("reserved"),
             QStringLiteral("tags"),
             QStringLiteral(
                 "description")}},
        {QStringLiteral("field"),
         QJsonArray{
             QStringLiteral("name"),
             QStringLiteral("lsb"),
             QStringLiteral("msb"),
             QStringLiteral("width"),
             QStringLiteral("type"),
             QStringLiteral(
                 "software_access"),
             QStringLiteral(
                 "hardware_access"),
             QStringLiteral(
                 "read_side_effect"),
             QStringLiteral(
                 "write_side_effect"),
             QStringLiteral("minimum"),
             QStringLiteral("maximum"),
             QStringLiteral(
                 "description")}},
        {QStringLiteral("enum"),
         QJsonArray{
             QStringLiteral("name"),
             QStringLiteral("value"),
             QStringLiteral(
                 "description")}},
    };
    const QJsonObject valueTokens{
        {QStringLiteral("access"),
         QJsonArray{
             QStringLiteral("none"),
             QStringLiteral("ro"),
             QStringLiteral("wo"),
             QStringLiteral("rw")}},
        {QStringLiteral("type"),
         QJsonArray{
             QStringLiteral("bits"),
             QStringLiteral("bool"),
             QStringLiteral("unsigned"),
             QStringLiteral("signed"),
             QStringLiteral("enum"),
             QStringLiteral("field"),
             QStringLiteral("reserved")}},
        {QStringLiteral(
             "read_side_effect"),
         QJsonArray{
             QStringLiteral("none"),
             QStringLiteral("clear"),
             QStringLiteral("set")}},
        {QStringLiteral(
             "write_side_effect"),
         QJsonArray{
             QStringLiteral("none"),
             QStringLiteral("write"),
             QStringLiteral("w1c"),
             QStringLiteral("w1s"),
             QStringLiteral("w0c"),
             QStringLiteral("w0s"),
             QStringLiteral("toggle")}},
    };
    const QJsonArray
        fieldCreationProperties =
            writableProperties.value(
                                  QStringLiteral(
                                      "field"))
                .toArray();
    const QJsonObject
        creationValueMembers{
            {QStringLiteral("page"),
             writableProperties.value(
                 QStringLiteral("page"))},
            {QStringLiteral("block"),
             writableProperties.value(
                 QStringLiteral("block"))},
            {QStringLiteral("register"),
             writableProperties.value(
                 QStringLiteral(
                     "register"))},
            {QStringLiteral("field"),
             fieldCreationProperties},
            {QStringLiteral("enum"),
             writableProperties.value(
                 QStringLiteral("enum"))},
        };
    const QJsonObject operationSchemas{
        {QStringLiteral("set"),
         QJsonObject{
             {QStringLiteral("required"),
              QJsonArray{
                  QStringLiteral("op"),
                  QStringLiteral("id"),
                  QStringLiteral(
                      "property"),
                  QStringLiteral(
                      "value")}},
             {QStringLiteral(
                  "id_contract"),
              QStringLiteral(
                  "stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
         }},
        {QStringLiteral("add"),
         QJsonObject{
             {QStringLiteral("required"),
              QJsonArray{
                  QStringLiteral("op"),
                  QStringLiteral("kind"),
                  QStringLiteral("id"),
                  QStringLiteral(
                      "parent_id"),
                  QStringLiteral(
                      "value")}},
             {QStringLiteral("kinds"),
              QJsonArray{
                  QStringLiteral("page"),
                  QStringLiteral("block"),
                  QStringLiteral(
                      "register"),
                  QStringLiteral("field"),
                  QStringLiteral("enum")}},
             {QStringLiteral(
                  "id_contract"),
              QStringLiteral(
                  "non-empty stable ID or the exact string 'auto' for a deterministic free ID derived from kind and name")},
             {QStringLiteral(
                  "parent_id_contract"),
              QStringLiteral(
                  "stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
             {QStringLiteral(
                  "required_value_members"),
              QJsonObject{
                  {QStringLiteral("page"),
                   QJsonArray{
                       QStringLiteral(
                           "name")}},
                  {QStringLiteral("block"),
                   QJsonArray{
                       QStringLiteral(
                           "name")}},
                  {QStringLiteral(
                       "register"),
                   QJsonArray{
                       QStringLiteral(
                           "name")}},
                  {QStringLiteral("field"),
                   QJsonArray{
                       QStringLiteral(
                           "name"),
                       QStringLiteral(
                           "lsb")}},
                  {QStringLiteral("enum"),
                   QJsonArray{
                       QStringLiteral(
                           "name"),
                       QStringLiteral(
                           "value")}},
              }},
             {QStringLiteral(
                  "allowed_value_members"),
              creationValueMembers},
             {QStringLiteral(
                  "parent_kinds"),
              QJsonObject{
                  {QStringLiteral("page"),
                   QStringLiteral(
                       "workspace")},
                  {QStringLiteral("block"),
                   QStringLiteral("page")},
                  {QStringLiteral(
                       "register"),
                   QStringLiteral("block")},
                  {QStringLiteral("field"),
                   QStringLiteral(
                       "structure register or field")},
                  {QStringLiteral("enum"),
                   QStringLiteral(
                       "bool or enum register or field")},
              }},
         }},
        {QStringLiteral("copy"),
         QJsonObject{
             {QStringLiteral("required"),
              QJsonArray{
                  QStringLiteral("op"),
                  QStringLiteral("id"),
                  QStringLiteral(
                      "new_id"),
                  QStringLiteral(
                      "parent_id")}},
             {QStringLiteral(
                  "id_contract"),
              QStringLiteral(
                  "source stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
             {QStringLiteral("kinds"),
              QJsonArray{
                  QStringLiteral("page"),
                  QStringLiteral("block"),
                  QStringLiteral(
                      "register"),
                  QStringLiteral("field"),
                  QStringLiteral("enum")}},
             {QStringLiteral(
                  "parent_id_contract"),
              QStringLiteral(
                  "stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
             {QStringLiteral(
                  "optional_members"),
              QJsonObject{
                  {QStringLiteral("value"),
                   QStringLiteral(
                       "root writable-property overrides")},
                  {QStringLiteral(
                       "unique_name"),
                   QStringLiteral(
                       "boolean; true assigns the first Workbench-style unique sibling name and cannot be combined with value.name")},
              }},
             {QStringLiteral(
                  "allowed_value_members"),
              creationValueMembers},
             {QStringLiteral(
                  "parent_kinds"),
              QJsonObject{
                  {QStringLiteral("page"),
                   QStringLiteral(
                       "workspace")},
                  {QStringLiteral("block"),
                   QStringLiteral("page")},
                  {QStringLiteral(
                       "register"),
                   QStringLiteral("block")},
                  {QStringLiteral("field"),
                   QStringLiteral(
                       "structure register or field")},
                  {QStringLiteral("enum"),
                   QStringLiteral(
                       "bool or enum register or field")},
              }},
             {QStringLiteral(
                  "descendant_id_rule"),
              QStringLiteral(
                  "<new_id>--<source-descendant-id>")},
             {QStringLiteral(
                  "clears_source_locations"),
              true},
             {QStringLiteral(
                  "returns_id_mapping"),
              true},
         }},
        {QStringLiteral("move"),
         QJsonObject{
             {QStringLiteral("required"),
              QJsonArray{
                  QStringLiteral("op"),
                  QStringLiteral("id"),
                  QStringLiteral(
                      "parent_id")}},
             {QStringLiteral(
                  "id_contract"),
              QStringLiteral(
                  "stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
             {QStringLiteral("kinds"),
              QJsonArray{
                  QStringLiteral("page"),
                  QStringLiteral("block"),
                  QStringLiteral(
                      "register"),
                  QStringLiteral("field"),
                  QStringLiteral("enum")}},
             {QStringLiteral(
                  "parent_id_contract"),
              QStringLiteral(
                  "stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
             {QStringLiteral(
                  "parent_kinds"),
              QJsonObject{
                  {QStringLiteral("page"),
                   QStringLiteral(
                       "workspace")},
                  {QStringLiteral("block"),
                   QStringLiteral("page")},
                  {QStringLiteral(
                       "register"),
                   QStringLiteral("block")},
                  {QStringLiteral("field"),
                   QStringLiteral(
                       "structure register or field")},
                  {QStringLiteral("enum"),
                   QStringLiteral(
                       "bool or enum register or field")},
              }},
             {QStringLiteral(
                  "preserves_stable_ids"),
              true},
             {QStringLiteral(
                  "optional_members"),
              QJsonObject{
                  {QStringLiteral(
                       "before_id"),
                   QStringLiteral(
                       "page/block/register sibling stable ID or earlier-operation reference; null means end; register reordering is same-parent only")},
                  {QStringLiteral(
                       "placement"),
                   QStringLiteral(
                       "the exact string 'auto' for block/register/field automatic placement")},
              }},
             {QStringLiteral(
                  "same_parent_without_before_id_is_no_op"),
              true},
         }},
        {QStringLiteral("remove"),
         QJsonObject{
             {QStringLiteral("required"),
              QJsonArray{
                  QStringLiteral("op"),
                  QStringLiteral("id")}},
             {QStringLiteral(
                  "id_contract"),
              QStringLiteral(
                  "stable ID string or {'operation': N} referencing an earlier zero-based patch operation result")},
             {QStringLiteral(
                  "cascade_required_for_descendants"),
              true},
         }},
    };
    return {
        {QStringLiteral("api_version"),
         apiVersion},
        {QStringLiteral("cli_version"),
         fromUtf8(cliVersion)},
        {QStringLiteral("project_schema"),
         projectSchemaJson()},
        {QStringLiteral(
             "json_transport"),
         jsonTransportContract()},
        {QStringLiteral("stable_id_required"),
         true},
        {QStringLiteral("object_kinds"),
         kinds},
        {QStringLiteral("commands"),
         commands},
        {QStringLiteral(
             "global_options"),
         globalOptions},
        {QStringLiteral(
             "command_argument_schemas"),
         commandArgumentSchemas},
        {QStringLiteral(
             "command_discovery_contract"),
         QJsonObject{
             {QStringLiteral(
                  "argument_schema"),
              QJsonObject{
                  {QStringLiteral(
                       "positionals"),
                   QStringLiteral(
                       "ordered positional parameters with stable names, value types, required flags, and optional repeatable flags")},
                  {QStringLiteral("options"),
                   QStringLiteral(
                       "recognized flags or value options with explicit repeatable metadata; allowed_values is present for closed enums")},
                  {QStringLiteral(
                       "mutually_exclusive_options"),
                   QStringLiteral(
                       "arrays of option names that cannot be used together")}}},
             {QStringLiteral(
                  "project_access"),
              QJsonObject{
                  {QStringLiteral("none"),
                   QStringLiteral(
                       "does not open a project")},
                  {QStringLiteral("read"),
                   QStringLiteral(
                       "opens one or more existing projects without modifying them")},
                  {QStringLiteral(
                       "create"),
                   QStringLiteral(
                       "creates a new project path")},
                  {QStringLiteral("write"),
                   QStringLiteral(
                       "can modify an existing project or its configured outputs")}}},
             {QStringLiteral(
                  "file_write_behavior"),
              QJsonObject{
                  {QStringLiteral("never"),
                   QStringLiteral(
                       "does not write files")},
                  {QStringLiteral("always"),
                   QStringLiteral(
                       "writes files on success")},
                  {QStringLiteral(
                       "unless_dry_run"),
                   QStringLiteral(
                       "does not write with --dry-run; otherwise writes on success")}}},
         }},
        {QStringLiteral(
             "workbench_navigation_contract"),
         QJsonObject{
             {QStringLiteral("member"),
              QStringLiteral(
                  "workbench_navigation")},
             {QStringLiteral("present_on"),
              QJsonArray{
                  QStringLiteral(
                      "list result item"),
                  QStringLiteral(
                      "find result item"),
                  QStringLiteral(
                      "get result object"),
                  QStringLiteral(
                      "get-many found item object"),
                  QStringLiteral(
                      "diff change before_navigation or after_navigation when that side exists")}},
             {QStringLiteral(
                  "argument_order"),
              QJsonArray{
                  QStringLiteral(
                      "--project"),
                  QStringLiteral(
                      "<absolute-project-path>"),
                  QStringLiteral(
                      "--select"),
                  QStringLiteral(
                      "<stable-id>")}},
             {QStringLiteral(
                  "protocol_version"),
              1},
             {QStringLiteral("fields"),
              QJsonArray{
                  QStringLiteral(
                      "protocol_version"),
                  QStringLiteral(
                      "project"),
                  QStringLiteral(
                      "stable_id"),
                  QStringLiteral(
                      "kind"),
                  QStringLiteral(
                      "path"),
                  QStringLiteral(
                      "arguments")}},
             {QStringLiteral(
                  "launches_process"),
              false},
         }},
        {QStringLiteral(
             "writable_properties"),
         writableProperties},
        {QStringLiteral(
             "value_tokens"),
         valueTokens},
        {QStringLiteral(
             "operation_schemas"),
         operationSchemas},
        {QStringLiteral(
             "creation_defaults"),
         QJsonObject{
             {QStringLiteral("page"),
              QJsonObject{
                  {QStringLiteral("base"),
                   QStringLiteral("0x0")},
                  {QStringLiteral(
                       "address_width"),
                   32}}},
             {QStringLiteral("block"),
              QJsonObject{
                  {QStringLiteral("base"),
                   QStringLiteral("0x0")},
                  {QStringLiteral("size"),
                   QJsonValue(
                       QJsonValue::Null)}}},
             {QStringLiteral(
                  "register"),
              QJsonObject{
                  {QStringLiteral("offset"),
                   QStringLiteral("0x0")},
                  {QStringLiteral("width"),
                   32},
                  {QStringLiteral("type"),
                   QStringLiteral(
                       "unsigned")},
                  {QStringLiteral("access"),
                   QStringLiteral("rw")},
                  {QStringLiteral(
                       "initial"),
                   QStringLiteral("0x0")},
                  {QStringLiteral("reset"),
                   QStringLiteral("0x0")}}},
             {QStringLiteral("field"),
              QJsonObject{
                  {QStringLiteral("width"),
                   1},
                  {QStringLiteral(
                       "software_access"),
                   QStringLiteral(
                       "inherited from containing register")}}},
         }},
        {QStringLiteral(
             "value_contracts"),
         QJsonObject{
             {QStringLiteral(
                  "unsigned"),
              QStringLiteral(
                  "unsigned integer literal string or exact JSON integer <= 2^53-1")},
             {QStringLiteral(
                  "optional"),
              QStringLiteral(
                  "use JSON null to clear")},
             {QStringLiteral("tags"),
              QStringLiteral(
                  "array of unique non-empty strings")},
             {QStringLiteral(
                  "field_lsb"),
              QStringLiteral(
                  "setting lsb preserves current width and derives msb; Workbench keeps direct LSB cell editing disabled")},
             {QStringLiteral(
                  "field_lsb_auto"),
              QStringLiteral(
                  "the exact string 'auto' selects the lowest contiguous free bit range for add or copy field; width is accepted and msb is not")},
             {QStringLiteral(
                  "block_base_auto"),
              QStringLiteral(
                  "the exact string 'auto' selects the lowest free Page-relative range for add or copy block and requires a positive explicit or copied size")},
             {QStringLiteral(
                  "register_offset_auto"),
              QStringLiteral(
                  "the exact string 'auto' selects the next aligned free range for add or copy register")},
             {QStringLiteral(
                  "move_placement_auto"),
              QStringLiteral(
                  "move placement accepts the exact string 'auto' for block, register, or field; omission preserves the current position")},
             {QStringLiteral(
                  "register_before_id_reorder"),
              QStringLiteral(
                  "a movable register may use before_id within its existing block; movable registers exchange sorted offset slots while fixed register offsets remain unchanged")},
             {QStringLiteral(
                  "copy_new_id_auto"),
              QStringLiteral(
                  "copy new_id accepts the exact string 'auto' to select a deterministic free root and descendant ID family for the current revision")},
             {QStringLiteral(
                  "add_id_auto"),
              QStringLiteral(
                  "add id accepts the exact string 'auto' to select a deterministic free stable ID derived from kind and name for the current revision")},
             {QStringLiteral(
                  "operation_parent_reference"),
              QStringLiteral(
                  "add, copy, and move parent_id accept {'operation': N} to use the object ID returned by an earlier zero-based operation in the same patch")},
             {QStringLiteral(
                  "operation_object_reference"),
              QStringLiteral(
                  "set, copy, move, and remove id plus move before_id accept {'operation': N} to use the object ID returned by an earlier zero-based operation in the same patch")},
             {QStringLiteral(
                  "operation_named_reference"),
              QStringLiteral(
                  "every operation may define a unique case-sensitive ref; object reference members accept {'ref': <name>} for an earlier operation")},
             {QStringLiteral(
                  "copy_unique_name"),
              QStringLiteral(
                  "copy unique_name true selects the first '<source> Copy' sibling name, adding a numeric suffix when needed")},
         }},
        {QStringLiteral("apply_patch"),
         QJsonObject{
             {QStringLiteral(
                  "api_version"),
              apiVersion},
             {QStringLiteral(
                  "expected_revision"),
              QStringLiteral(
                  "sha256:<revision from summary/get/list/validate>")},
             {QStringLiteral(
                  "operations"),
              QJsonArray{
                  QJsonObject{
                      {QStringLiteral(
                           "op"),
                       QStringLiteral(
                           "set")},
                      {QStringLiteral(
                           "id"),
                       QStringLiteral(
                           "<stable-id>")},
                      {QStringLiteral(
                           "property"),
                       QStringLiteral(
                           "description")},
                      {QStringLiteral(
                           "value"),
                       QStringLiteral(
                           "Updated text")},
                  }}},
         }},
        {QStringLiteral(
             "write_requires_revision"),
         true},
        {QStringLiteral(
             "operation_reference_contract"),
         QJsonObject{
             {QStringLiteral(
                  "definition_member"),
              QStringLiteral("ref")},
             {QStringLiteral(
                  "definition"),
              QStringLiteral(
                  "optional unique non-empty string without leading or trailing whitespace")},
             {QStringLiteral(
                  "reference_forms"),
              QJsonArray{
                  QStringLiteral(
                      "{'operation': <earlier zero-based index>}"),
                  QStringLiteral(
                      "{'ref': <earlier operation ref>}")}},
             {QStringLiteral(
                  "case_sensitive"),
              true},
             {QStringLiteral("unique"),
              true},
             {QStringLiteral(
                  "earlier_operations_only"),
              true},
             {QStringLiteral(
                  "change_result_echoes_ref"),
              true},
         }},
        {QStringLiteral(
             "apply_operation_failure_contract"),
         QJsonObject{
             {QStringLiteral(
                  "result_member"),
              QStringLiteral("failure")},
             {QStringLiteral("stage"),
              QStringLiteral(
                  "operation")},
             {QStringLiteral(
                  "indexing"),
              QStringLiteral(
                  "zero-based")},
             {QStringLiteral(
                  "members"),
              QJsonArray{
                  QStringLiteral(
                      "stage"),
                  QStringLiteral(
                      "operation_index"),
                  QStringLiteral(
                      "json_pointer"),
                  QStringLiteral(
                      "operation_count"),
                  QStringLiteral(
                      "completed_operation_count"),
                  QStringLiteral(
                      "operation"),
                  QStringLiteral(
                      "message"),
                  QStringLiteral(
                      "writes_performed"),
                  QStringLiteral(
                      "completed_operations_rolled_back")}},
             {QStringLiteral(
                  "writes_performed"),
              false},
             {QStringLiteral(
                  "completed_operations_rolled_back"),
             true},
         }},
        {QStringLiteral(
             "apply_prewrite_failure_contract"),
         QJsonObject{
             {QStringLiteral(
                  "result_member"),
              QStringLiteral("failure")},
             {QStringLiteral("stages"),
              QJsonArray{
                  QStringLiteral(
                      "validation"),
                  QStringLiteral(
                      "generation")}},
             {QStringLiteral(
                  "diagnostic_index_member"),
              QStringLiteral(
                  "error_diagnostic_indexes")},
             {QStringLiteral(
                  "diagnostic_index_target"),
              QStringLiteral(
                  "top-level diagnostics array")},
             {QStringLiteral(
                  "operation_index"),
              QJsonValue(
                  QJsonValue::Null)},
             {QStringLiteral(
                  "writes_performed"),
              false},
             {QStringLiteral(
                  "completed_operations_rolled_back"),
              true},
         }},
        {QStringLiteral(
             "partial_success_contract"),
         QJsonObject{
             {QStringLiteral(
                  "condition"),
              QStringLiteral(
                  "apply or init result has saved=true and generated=false")},
             {QStringLiteral(
                  "meaning"),
              QStringLiteral(
                  "the model is saved but one or more read-only outputs may be stale")},
             {QStringLiteral(
                  "recovery_command"),
              QStringLiteral(
                  "generate")},
             {QStringLiteral(
                  "recovery_members"),
              QJsonArray{
                  QStringLiteral(
                      "command"),
                  QStringLiteral(
                      "project"),
                  QStringLiteral(
                      "reason"),
                  QStringLiteral(
                      "expected_revision"),
                  QStringLiteral(
                      "arguments")}},
             {QStringLiteral(
                  "revision_guard"),
              QStringLiteral(
                  "expected_revision and --expect are included whenever the saved revision is available")},
         }},
        {QStringLiteral(
             "generation_revision_guard"),
         QJsonObject{
             {QStringLiteral(
                  "expect_option"),
              QStringLiteral(
                  "--expect <sha256:...>")},
             {QStringLiteral(
                  "automatic_recheck"),
              QStringLiteral(
                  "project revision is checked before and after output writes")},
             {QStringLiteral(
                  "outputs_current"),
              QStringLiteral(
                  "true only when every output was written from the current project revision")},
             {QStringLiteral(
                  "recovery_member"),
              QStringLiteral(
                  "result.recovery")},
             {QStringLiteral(
                  "recovery_present_when"),
              QStringLiteral(
                  "output writing fails or the project changes after writing, and a current revision is available")},
             {QStringLiteral(
                  "recovery_revision"),
              QStringLiteral(
                  "the rechecked current revision, guarded by --expect")},
         }},
        {QStringLiteral(
             "output_status_contract"),
         QJsonObject{
             {QStringLiteral(
                  "artifact_members"),
              QJsonArray{
                  QStringLiteral("kind"),
                  QStringLiteral("path"),
                  QStringLiteral("bytes"),
                  QStringLiteral("sha256"),
                  QStringLiteral("state"),
                  QStringLiteral("exists"),
                  QStringLiteral(
                      "content_current"),
                  QStringLiteral(
                      "read_only"),
                  QStringLiteral(
                      "synchronized")}},
             {QStringLiteral(
                  "write_report_members"),
              QJsonArray{
                  QStringLiteral("action"),
                  QStringLiteral("written"),
                  QStringLiteral("skipped"),
                  QStringLiteral("failed")}},
             {QStringLiteral(
                  "hash_meaning"),
              QStringLiteral(
                  "sha256 is the expected generated artifact content hash")},
             {QStringLiteral(
                  "states"),
              QJsonArray{
                  QStringLiteral(
                      "synchronized"),
                  QStringLiteral(
                      "missing"),
                  QStringLiteral(
                      "modified"),
                  QStringLiteral(
                      "writable"),
                  QStringLiteral(
                      "unreadable")}},
             {QStringLiteral(
                  "stale_is_command_error"),
              false},
             {QStringLiteral(
                  "strict_option"),
              QStringLiteral(
                  "--require-current")},
             {QStringLiteral(
                  "strict_exit_code"),
              static_cast<int>(
                  ExitCode::
                      outputsOutOfDate)},
             {QStringLiteral(
                  "strict_error_code"),
              fromUtf8(
                  outputsOutOfDateCode)},
             {QStringLiteral(
                  "recovery_member"),
              QStringLiteral(
                  "result.recovery")},
             {QStringLiteral(
                  "recovery_present_when"),
              QStringLiteral(
                  "attention_count is greater than zero")},
             {QStringLiteral(
                  "recovery_members"),
              QJsonArray{
                  QStringLiteral(
                      "command"),
                  QStringLiteral(
                      "project"),
                  QStringLiteral(
                      "expected_revision"),
                  QStringLiteral(
                      "arguments")}},
             {QStringLiteral(
                  "stable_revision_check"),
              true},
         }},
        {QStringLiteral(
             "find_contract"),
         QJsonObject{
             {QStringLiteral(
                  "searches"),
              QJsonArray{
                  QStringLiteral("id"),
                  QStringLiteral("name"),
                  QStringLiteral("path"),
                  QStringLiteral("base"),
                  QStringLiteral("address"),
                  QStringLiteral("offset"),
                  QStringLiteral("size"),
                  QStringLiteral(
                      "address_width"),
                  QStringLiteral("msb"),
                  QStringLiteral("lsb"),
                  QStringLiteral("width"),
                  QStringLiteral("type"),
                  QStringLiteral("value"),
                  QStringLiteral("tags"),
                  QStringLiteral(
                      "description")}},
             {QStringLiteral(
                  "case_sensitive"),
              false},
             {QStringLiteral(
                  "exact_option"),
              QStringLiteral("--exact")},
             {QStringLiteral(
                  "exact_match_ranks"),
              QJsonArray{0, 1}},
             {QStringLiteral(
                  "exact_scope"),
              QStringLiteral(
                  "case-insensitive exact id, name, path, or property value")},
             {QStringLiteral(
                  "exact_excludes"),
              QJsonArray{
                  QStringLiteral(
                      "id or name prefix"),
                  QStringLiteral(
                      "substring")}},
             {QStringLiteral(
                  "require_one_option"),
              QStringLiteral("--require-one")},
             {QStringLiteral(
                  "require_one_after_filters"),
              true},
             {QStringLiteral(
                  "require_one_conflicts"),
             QJsonArray{
                  QStringLiteral("--offset"),
                  QStringLiteral("--limit"),
                  QStringLiteral("--all")}},
             {QStringLiteral(
                  "require_one_zero_error_code"),
              QStringLiteral("RMC2001")},
             {QStringLiteral(
                  "require_one_many_error_code"),
              QStringLiteral("RMC2002")},
             {QStringLiteral(
                  "require_one_many_preserves_candidates"),
              true},
             {QStringLiteral(
                  "limit_option"),
              QStringLiteral(
                  "--limit <positive integer>")},
             {QStringLiteral(
                  "all_option"),
              QStringLiteral("--all")},
             {QStringLiteral(
                  "default_limit"),
              static_cast<qint64>(
                  defaultQueryLimit)},
             {QStringLiteral(
                  "offset_option"),
              QStringLiteral(
                  "--offset <non-negative integer>")},
             {QStringLiteral(
                  "result_metadata"),
              QJsonArray{
                  QStringLiteral(
                      "total_count"),
                  QStringLiteral(
                      "offset"),
                  QStringLiteral(
                      "returned_count"),
                  QStringLiteral(
                      "truncated"),
                  QStringLiteral(
                      "has_more"),
                  QStringLiteral(
                      "next_offset"),
                  QStringLiteral(
                      "limit")}},
             {QStringLiteral(
                  "match_result_members"),
              QJsonArray{
                  QStringLiteral(
                      "match_rank"),
                  QStringLiteral(
                      "match_field")}},
             {QStringLiteral(
                  "ranking"),
             QJsonArray{
                  QStringLiteral(
                      "exact id or name"),
                  QStringLiteral(
                      "exact path or property"),
                  QStringLiteral(
                      "id or name prefix"),
                  QStringLiteral(
                      "substring")}},
         }},
        {QStringLiteral(
             "pagination_contract"),
         QJsonObject{
             {QStringLiteral(
                  "commands"),
              QJsonArray{
                  QStringLiteral("list"),
                  QStringLiteral("find"),
                  QStringLiteral("diff")}},
             {QStringLiteral(
                  "default_offset"),
              0},
             {QStringLiteral(
                  "default_limit"),
              QJsonObject{
                  {QStringLiteral("list"),
                   static_cast<qint64>(
                       defaultQueryLimit)},
                  {QStringLiteral("find"),
                   static_cast<qint64>(
                       defaultQueryLimit)},
                  {QStringLiteral("diff"),
                   QJsonValue(
                       QJsonValue::Null)}}},
             {QStringLiteral(
                  "unbounded_option"),
              QStringLiteral("--all")},
             {QStringLiteral(
                  "stable_order"),
              true},
             {QStringLiteral(
                  "revision_guard_option"),
              QStringLiteral(
                  "--expect <sha256:...>")},
             {QStringLiteral(
                  "diff_revision_guard_options"),
              QJsonArray{
                  QStringLiteral(
                      "--expect-before <sha256:...>"),
                  QStringLiteral(
                      "--expect-after <sha256:...>")}},
             {QStringLiteral(
                  "revision_conflict_exit_code"),
              static_cast<int>(
                  ExitCode::
                      revisionConflict)},
             {QStringLiteral(
                  "continuation_member"),
              QStringLiteral(
                  "result_metadata.next_offset")},
             {QStringLiteral(
                  "result_metadata"),
              QJsonArray{
                  QStringLiteral(
                      "total_count"),
                  QStringLiteral(
                      "offset"),
                  QStringLiteral(
                      "returned_count"),
                  QStringLiteral(
                      "truncated"),
                  QStringLiteral(
                      "has_more"),
                  QStringLiteral(
                      "next_offset"),
                  QStringLiteral(
                      "limit")}},
         }},
        {QStringLiteral(
             "query_filter_contract"),
         QJsonObject{
             {QStringLiteral(
                  "parent_scope"),
              QStringLiteral(
                  "direct children of an existing stable ID")},
             {QStringLiteral(
                  "recursive_option"),
              QStringLiteral(
                  "--recursive")},
             {QStringLiteral(
                  "recursive_scope"),
              QStringLiteral(
                  "all descendants of the --parent stable ID, excluding the parent itself")},
             {QStringLiteral(
                  "recursive_requires_parent"),
              true},
             {QStringLiteral(
                  "missing_parent"),
              QStringLiteral(
                  "project error; never treated as an empty result")},
             {QStringLiteral(
                  "duplicate_option"),
              QStringLiteral(
                  "usage error")},
         }},
        {QStringLiteral(
             "tag_filter_contract"),
         QJsonObject{
             {QStringLiteral("commands"),
              QJsonArray{
                  QStringLiteral("list"),
                  QStringLiteral("find")}},
             {QStringLiteral("option"),
              QStringLiteral(
                  "--tag <exact tag>")},
             {QStringLiteral("matching"),
              QStringLiteral(
                  "exact and case-sensitive")},
             {QStringLiteral(
                  "missing_tag"),
              QStringLiteral(
                  "successful empty result")},
             {QStringLiteral(
                  "catalog_member"),
              QStringLiteral(
                  "summary.result.tags")},
             {QStringLiteral(
                  "catalog_order"),
              QStringLiteral(
                  "case-sensitive lexical")},
             {QStringLiteral(
                  "catalog_item_members"),
              QJsonArray{
                  QStringLiteral("name"),
                  QStringLiteral(
                      "register_count")}},
         }},
        {QStringLiteral(
             "diff_contract"),
         QJsonObject{
             {QStringLiteral(
                  "result_members"),
              QJsonArray{
                  QStringLiteral(
                      "before_project"),
                  QStringLiteral(
                      "after_project"),
                  QStringLiteral(
                      "before_revision"),
                  QStringLiteral(
                      "after_revision"),
                  QStringLiteral(
                      "kind_filter"),
                  QStringLiteral(
                      "require_equal"),
                  QStringLiteral("changed"),
                  QStringLiteral(
                      "change_count"),
                  QStringLiteral("counts"),
                  QStringLiteral("changes"),
                  QStringLiteral(
                      "writes_performed")}},
             {QStringLiteral(
                  "change_members"),
              QJsonArray{
                  QStringLiteral("change"),
                  QStringLiteral(
                      "object_kind"),
                  QStringLiteral("id"),
                  QStringLiteral("name"),
                  QStringLiteral("summary"),
                  QStringLiteral(
                      "before_source"),
                  QStringLiteral(
                      "after_source"),
                  QStringLiteral("before"),
                  QStringLiteral("after"),
                  QStringLiteral(
                      "property_changes"),
                  QStringLiteral(
                      "before_navigation"),
                  QStringLiteral(
                      "after_navigation")}},
             {QStringLiteral(
                  "state_members"),
              QJsonArray{
                  QStringLiteral("kind"),
                  QStringLiteral("id"),
                  QStringLiteral(
                      "parent_id"),
                  QStringLiteral("order"),
                  QStringLiteral(
                      "properties")}},
             {QStringLiteral(
                  "property_change_members"),
              QJsonArray{
                  QStringLiteral(
                      "property"),
                  QStringLiteral(
                      "before_present"),
                  QStringLiteral("before"),
                  QStringLiteral(
                      "after_present"),
                  QStringLiteral("after")}},
             {QStringLiteral(
                  "change_kinds"),
              QJsonArray{
                  QStringLiteral("added"),
                  QStringLiteral("removed"),
                  QStringLiteral(
                      "modified")}},
             {QStringLiteral(
                  "stable_id_matching"),
              true},
             {QStringLiteral(
                  "stable_order"),
              QStringLiteral(
                  "lexicographic stable ID")},
             {QStringLiteral(
                  "missing_source_side_is_null"),
              true},
             {QStringLiteral(
                  "single_project_envelope_members_omitted"),
              true},
             {QStringLiteral(
                  "rechecks_both_revisions"),
              true},
             {QStringLiteral(
                  "pagination"),
              QStringLiteral(
                  "changes contains the requested page; change_count and counts describe the complete kind-filtered result")},
             {QStringLiteral(
                  "strict_option"),
              QStringLiteral(
                  "--require-equal")},
             {QStringLiteral(
                  "strict_exit_code"),
              static_cast<int>(
                  ExitCode::
                      differencesFound)},
             {QStringLiteral(
                  "strict_error_code"),
              fromUtf8(
                  differencesFoundCode)},
             {QStringLiteral(
                  "strict_scope"),
              QStringLiteral(
                  "the complete kind-filtered comparison, independent of pagination")},
         }},
        {QStringLiteral(
             "get_contract"),
         QJsonObject{
             {QStringLiteral(
                  "omitted_stable_id"),
              QStringLiteral(
                  "returns the complete root Workspace hierarchy")},
             {QStringLiteral(
                  "explicit_stable_id"),
              QStringLiteral(
                  "returns the identified object and its descendants")},
             {QStringLiteral(
                  "revision_guard_option"),
              QStringLiteral(
                  "--expect <sha256:...>")},
             {QStringLiteral(
                  "revision_mismatch"),
              QStringLiteral(
                  "revision conflict with no object result and no writes")},
         }},
        {QStringLiteral(
             "get_many_contract"),
         QJsonObject{
             {QStringLiteral(
                  "result_members"),
              QJsonArray{
                  QStringLiteral(
                      "requested_count"),
                  QStringLiteral(
                      "found_count"),
                  QStringLiteral(
                      "missing_count"),
                  QStringLiteral(
                      "duplicate_count"),
                  QStringLiteral("items")}},
             {QStringLiteral(
                  "item_members"),
              QJsonArray{
                  QStringLiteral(
                      "request_index"),
                  QStringLiteral(
                      "requested_id"),
                  QStringLiteral("found"),
                  QStringLiteral("object"),
                  QStringLiteral(
                      "error_code"),
                  QStringLiteral(
                      "duplicate_of_index")}},
             {QStringLiteral("order"),
              QStringLiteral(
                  "request order; duplicate stable IDs remain separate items")},
             {QStringLiteral(
                  "found_count"),
              QStringLiteral(
                  "number of found request occurrences, including duplicates")},
             {QStringLiteral(
                  "missing_behavior"),
              QStringLiteral(
                  "project error RMC2001 while preserving found items and one item for every missing request")},
             {QStringLiteral(
                  "revision_guard_option"),
              QStringLiteral(
                  "--expect <sha256:...>")},
             {QStringLiteral(
                  "revision_mismatch"),
              QStringLiteral(
                  "revision conflict with no items or objects and no writes")},
         }},
        {QStringLiteral(
             "invalid_project_repair_contract"),
         QJsonObject{
             {QStringLiteral(
                  "apply_can_start_from_validation_errors"),
              true},
             {QStringLiteral(
                  "candidate_requirement"),
              QStringLiteral(
                  "all validation errors must be resolved before any file is written")},
             {QStringLiteral(
                  "new_problem_policy"),
              QStringLiteral(
                  "a repair may remove existing diagnostics but may not introduce a different diagnostic")},
             {QStringLiteral(
                  "atomic_failure"),
              QStringLiteral(
                  "remaining or introduced problems leave the project and outputs unchanged")},
             {QStringLiteral(
                  "result_member"),
              QStringLiteral("repair")},
         }},
        {QStringLiteral("exit_codes"),
         QJsonObject{
             {QStringLiteral("0"),
              QStringLiteral("success")},
             {QStringLiteral("1"),
              QStringLiteral(
                  "project or validation error")},
             {QStringLiteral("2"),
              QStringLiteral(
                  "command usage error")},
             {QStringLiteral("3"),
              QStringLiteral(
                  "revision conflict")},
             {QStringLiteral("4"),
              QStringLiteral(
                  "filesystem write error")},
             {QStringLiteral("5"),
              QStringLiteral(
                  "configured outputs are not current when --require-current is used")},
             {QStringLiteral("6"),
              QStringLiteral(
                  "project differences exist when diff --require-equal is used")},
             {QStringLiteral("7"),
              QStringLiteral(
                  "patch input could not be read or parsed")},
             {QStringLiteral("8"),
              QStringLiteral(
                  "artifact generation failed before writing")},
         }},
        {QStringLiteral(
             "no_command_contract"),
         QJsonObject{
             {QStringLiteral(
                  "exit_code"),
              static_cast<int>(
                  ExitCode::usageError)},
             {QStringLiteral(
                  "exit_status"),
              QStringLiteral(
                  "usage_error")},
             {QStringLiteral(
                  "json_command"),
              QString{}},
             {QStringLiteral(
                  "explicit_help_exit_code"),
              static_cast<int>(
                  ExitCode::success)},
         }},
        {QStringLiteral(
             "command_help_contract"),
         QJsonObject{
             {QStringLiteral(
                  "usage"),
              QStringLiteral(
                  "regmapc [--json] help [command]")},
             {QStringLiteral(
                  "project_access"),
              QStringLiteral("none")},
             {QStringLiteral(
                  "file_write_behavior"),
              QStringLiteral("never")},
             {QStringLiteral(
                  "focused_result_members"),
              QJsonArray{
                  QStringLiteral(
                      "target_command"),
                  QStringLiteral("usage"),
                  QStringLiteral(
                      "project_access"),
                  QStringLiteral(
                      "file_write_behavior"),
                  QStringLiteral(
                      "argument_schema")}},
             {QStringLiteral(
                  "unknown_topic_suggestion_member"),
              QStringLiteral(
                  "result.suggested_command")},
             {QStringLiteral(
                  "global_help_remains_complete"),
              true},
             {QStringLiteral(
                  "help_option_without_command"),
              QStringLiteral(
                  "returns the complete discovery document")},
             {QStringLiteral(
                  "help_option_with_command"),
              QStringLiteral(
                  "returns focused help for the first command token without opening its project arguments")},
         }},
        {QStringLiteral(
             "unknown_command_contract"),
         QJsonObject{
             {QStringLiteral(
                  "maximum_edit_distance"),
              2},
             {QStringLiteral(
                  "suggestion_member"),
              QStringLiteral(
                  "result.suggested_command")},
             {QStringLiteral(
                  "suggestion_absent"),
              QStringLiteral(
                  "result remains null when no known command is sufficiently close")},
         }},
        {QStringLiteral(
             "unknown_option_contract"),
         QJsonObject{
             {QStringLiteral(
                  "maximum_edit_distance"),
              2},
             {QStringLiteral(
                  "suggestion_member"),
              QStringLiteral(
                  "result.suggested_option")},
             {QStringLiteral(
                  "text_feedback"),
              QStringLiteral(
                  "the error also names the suggested option")},
             {QStringLiteral(
                  "allowed_option_source"),
              QStringLiteral(
                  "command_argument_schemas.<command>.options")},
         }},
        {QStringLiteral(
             "response_envelope_contract"),
         QJsonObject{
             {QStringLiteral(
                  "always_present"),
              QJsonArray{
                  QStringLiteral(
                      "api_version"),
                  QStringLiteral("command"),
                  QStringLiteral("ok"),
                  QStringLiteral(
                      "exit_code"),
                  QStringLiteral(
                      "exit_status"),
                  QStringLiteral(
                      "diagnostics")}},
             {QStringLiteral(
                  "exit_status_by_code"),
              QJsonObject{
                  {QStringLiteral("0"),
                   QStringLiteral(
                       "success")},
                  {QStringLiteral("1"),
                   QStringLiteral(
                       "project_error")},
                  {QStringLiteral("2"),
                   QStringLiteral(
                       "usage_error")},
                  {QStringLiteral("3"),
                   QStringLiteral(
                       "revision_conflict")},
                  {QStringLiteral("4"),
                   QStringLiteral(
                       "write_error")},
                  {QStringLiteral("5"),
                   QStringLiteral(
                       "outputs_out_of_date")},
                  {QStringLiteral("6"),
                   QStringLiteral(
                       "differences_found")},
                  {QStringLiteral("7"),
                   QStringLiteral(
                       "input_error")},
                  {QStringLiteral("8"),
                   QStringLiteral(
                       "generation_error")}}},
             {QStringLiteral(
                  "failure_member"),
              QStringLiteral(
                  "error_code")},
             {QStringLiteral(
                  "failure_member_present_when"),
              QStringLiteral(
                  "exit_code is nonzero")},
             {QStringLiteral(
                  "usage_recovery_member"),
              QStringLiteral("usage")},
             {QStringLiteral(
                  "usage_recovery_present_when"),
              QStringLiteral(
                  "exit_status is usage_error")},
             {QStringLiteral(
                  "primary_error_code_rule"),
              QStringLiteral(
                  "first error diagnostic code, otherwise the generic code for the exit category")},
             {QStringLiteral(
                  "generic_error_codes"),
              QJsonObject{
                  {QStringLiteral(
                       "usage_error"),
                   fromUtf8(
                       genericUsageFailureCode)},
                  {QStringLiteral(
                       "project_error"),
                   fromUtf8(
                       genericProjectFailureCode)},
                  {QStringLiteral(
                       "revision_conflict"),
                   fromUtf8(
                       genericRevisionFailureCode)},
                  {QStringLiteral(
                       "write_error"),
                   fromUtf8(
                       genericWriteFailureCode)},
                  {QStringLiteral(
                       "outputs_out_of_date"),
                   fromUtf8(
                       genericOutputsOutOfDateCode)},
                  {QStringLiteral(
                       "differences_found"),
                   fromUtf8(
                       genericDifferencesFoundCode)},
                  {QStringLiteral(
                       "input_error"),
                   fromUtf8(
                       genericInputFailureCode)},
                  {QStringLiteral(
                       "generation_error"),
                   fromUtf8(
                       genericGenerationFailureCode)}}},
         }},
    };
}

[[nodiscard]] QString usageText()
{
    return QStringLiteral(
        "Register Map Workbench CLI\n"
        "\n"
        "Usage:\n"
        "  regmapc [--json] help [command]\n"
        "  regmapc [--json] version\n"
        "  regmapc [--json] schema\n"
        "  regmapc [--json] init <project.regmap.yaml> [--name <workspace-name>] [--workspace-id <stable-id>] [--output-dir <relative-path>] [--target <target>]... [--xlsx-file <relative-path>] [--c-header-file <relative-path>] [--markdown-file <relative-path>] [--no-generate]\n"
        "  regmapc [--json] summary <project.regmap.yaml>\n"
        "  regmapc [--json] list <project.regmap.yaml> [--kind <kind>] [--parent <stable-id> [--recursive]] [--tag <tag>] [--offset <count>] [--limit <count> | --all] [--expect <sha256:...>]\n"
        "  regmapc [--json] find <project.regmap.yaml> <query> [--exact] [--require-one] [--kind <kind>] [--parent <stable-id> [--recursive]] [--tag <tag>] [--offset <count>] [--limit <count> | --all] [--expect <sha256:...>]\n"
        "  regmapc [--json] get <project.regmap.yaml> [stable-id] [--expect <sha256:...>]\n"
        "  regmapc [--json] get-many <project.regmap.yaml> <stable-id>... [--expect <sha256:...>]\n"
        "  regmapc [--json] validate <project.regmap.yaml>\n"
        "  regmapc [--json] diff <before.regmap.yaml> <after.regmap.yaml> [--kind <kind>] [--offset <count>] [--limit <count>] [--expect-before <sha256:...>] [--expect-after <sha256:...>] [--require-equal]\n"
        "  regmapc [--json] status <project.regmap.yaml> [--target <target>]... [--require-current]\n"
        "  regmapc [--json] generate <project.regmap.yaml> [--target <target>]... [--dry-run] [--expect <sha256:...>]\n"
        "  regmapc [--json] apply <project.regmap.yaml> <patch.json|-> [--dry-run] [--expect <sha256:...> | --force]\n"
        "\n"
        "Object kinds: workspace, page, block, register, field, enum, all\n"
        "Generation targets: xlsx, c-header, markdown\n"
        "list and find return at most 100 results unless --limit or --all is explicit.\n"
        "Use --json for a stable API-versioned response envelope.\n"
        "Use -- before a positional value that begins with --.\n"
        "Patch operations may define a unique ref; object IDs, parent IDs, and move anchors accept an earlier {\"operation\": N} or {\"ref\": \"name\"}; add id may be \"auto\".\n"
        "A real apply requires an expected revision unless --force is explicit.\n");
}

[[nodiscard]] QString usageForCommand(
    const QString& command)
{
    const QJsonArray commands =
        schemaJson()
            .value(
                QStringLiteral("commands"))
            .toArray();
    for (const QJsonValue& value :
         commands) {
        const QJsonObject descriptor =
            value.toObject();
        if (descriptor
                .value(
                    QStringLiteral("name"))
                .toString() ==
            command) {
            return descriptor
                .value(
                    QStringLiteral("usage"))
                .toString();
        }
    }
    return usageText();
}

[[nodiscard]] qsizetype editDistance(
    const QString& left,
    const QString& right)
{
    std::vector<qsizetype> previous(
        static_cast<std::size_t>(
            right.size() + 1));
    std::vector<qsizetype> current(
        previous.size());
    for (qsizetype index = 0;
         index <= right.size();
         ++index) {
        previous[
            static_cast<std::size_t>(
                index)] = index;
    }
    for (qsizetype leftIndex = 1;
         leftIndex <= left.size();
         ++leftIndex) {
        current.front() = leftIndex;
        for (qsizetype rightIndex = 1;
             rightIndex <= right.size();
             ++rightIndex) {
            const qsizetype deletion =
                previous[
                    static_cast<std::size_t>(
                        rightIndex)] +
                1;
            const qsizetype insertion =
                current[
                    static_cast<std::size_t>(
                        rightIndex - 1)] +
                1;
            const qsizetype substitution =
                previous[
                    static_cast<std::size_t>(
                        rightIndex - 1)] +
                (left.at(leftIndex - 1) ==
                         right.at(
                             rightIndex - 1)
                     ? 0
                     : 1);
            current[
                static_cast<std::size_t>(
                    rightIndex)] =
                std::min(
                    {deletion,
                     insertion,
                     substitution});
        }
        std::swap(
            previous,
            current);
    }
    return previous.back();
}

[[nodiscard]] QString suggestedCommand(
    QString command)
{
    if (command.startsWith(
            QStringLiteral("--"))) {
        command.remove(0, 2);
    }
    QString suggestion;
    qsizetype bestDistance = 3;
    const QJsonArray commands =
        schemaJson()
            .value(
                QStringLiteral("commands"))
            .toArray();
    for (const QJsonValue& value :
         commands) {
        const QString candidate =
            value.toObject()
                .value(
                    QStringLiteral("name"))
                .toString();
        const qsizetype distance =
            editDistance(
                command,
                candidate);
        if (distance <
            bestDistance) {
            bestDistance =
                distance;
            suggestion =
                candidate;
        }
    }
    return bestDistance <= 2
        ? suggestion
        : QString{};
}

[[nodiscard]] QString suggestedOption(
    const QString& command,
    const QString& option)
{
    QString suggestion;
    qsizetype bestDistance = 3;
    const QJsonArray options =
        schemaJson()
            .value(
                QStringLiteral(
                    "command_argument_schemas"))
            .toObject()
            .value(command)
            .toObject()
            .value(
                QStringLiteral("options"))
            .toArray();
    for (const QJsonValue& value :
         options) {
        const QString candidate =
            value.toObject()
                .value(
                    QStringLiteral("name"))
                .toString();
        const qsizetype distance =
            editDistance(
                option,
                candidate);
        if (distance <
            bestDistance) {
            bestDistance =
                distance;
            suggestion =
                candidate;
        }
    }
    return bestDistance <= 2
        ? suggestion
        : QString{};
}

[[nodiscard]] QString unknownOptionMessage(
    const QString& command,
    const QString& option)
{
    const QString suggestion =
        suggestedOption(
            command,
            option);
    return suggestion.isEmpty()
        ? QStringLiteral(
              "Unknown %1 option '%2'.")
              .arg(
                  command,
                  option)
        : QStringLiteral(
              "Unknown %1 option '%2'. Did you mean '%3'?")
              .arg(
                  command,
                  option,
                  suggestion);
}

void writeJsonResponse(
    const CommandResponse& response,
    QTextStream& output)
{
    QJsonObject root{
        {QStringLiteral("api_version"),
         apiVersion},
        {QStringLiteral("command"),
         response.command},
        {QStringLiteral("ok"),
         response.ok},
        {QStringLiteral("exit_code"),
         static_cast<int>(
             response.exitCode)},
        {QStringLiteral("exit_status"),
         exitStatus(
             response.exitCode)},
        {QStringLiteral("diagnostics"),
         diagnosticsJson(
             response.diagnostics)},
    };
    const QString errorCode =
        primaryErrorCode(response);
    if (!errorCode.isEmpty()) {
        root.insert(
            QStringLiteral("error_code"),
            errorCode);
    }
    if (!response.project.isEmpty()) {
        root.insert(
            QStringLiteral("project"),
            response.project);
    }
    if (!response.revision.isEmpty()) {
        root.insert(
            QStringLiteral("revision"),
            response.revision);
    }
    if (!response.usage.isEmpty()) {
        root.insert(
            QStringLiteral("usage"),
            response.usage);
    }
    if (!response.result.isUndefined()) {
        root.insert(
            QStringLiteral("result"),
            response.result);
    }
    if (!response.resultMetadata
             .isEmpty()) {
        root.insert(
            QStringLiteral(
                "result_metadata"),
            response.resultMetadata);
    }
    if (!response.error.isEmpty()) {
        root.insert(
            QStringLiteral("error"),
            response.error);
    }
    output
        << QJsonDocument(root).toJson(
               QJsonDocument::Compact)
        << '\n';
    output.flush();
}

void writeDiagnosticsText(
    const std::vector<Diagnostic>& diagnostics,
    QTextStream& error)
{
    for (const auto& diagnostic :
         diagnostics) {
        error
            << severityText(
                   diagnostic.severity)
                   .toUpper()
            << ' '
            << fromUtf8(
                   diagnostic.code)
            << ": "
            << fromUtf8(
                   diagnostic.message);
        if (!diagnostic.objectId.empty()) {
            error
                << " ["
                << fromUtf8(
                       diagnostic.objectId)
                << ']';
        }
        error << '\n';
    }
    error.flush();
}

void writeTextResponse(
    const CommandResponse& response,
    QTextStream& output,
    QTextStream& error)
{
    const bool deferDiagnostics =
        response.command ==
            QStringLiteral("find") &&
        std::ranges::any_of(
            response.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code ==
                    ambiguousFindMatchCode;
            });
    if (!deferDiagnostics) {
        writeDiagnosticsText(
            response.diagnostics,
            error);
    }
    const QJsonObject recovery =
        response.result.toObject()
            .value(
                QStringLiteral(
                    "recovery"))
            .toObject();
    const auto writeRecovery =
        [&]() {
            if (recovery.isEmpty()) {
                return;
            }
            QString project =
                recovery.value(
                            QStringLiteral(
                                "project"))
                    .toString();
            project.replace(
                QLatin1Char('"'),
                QStringLiteral(
                    "\\\""));
            error
                << "Recovery: regmapc "
                << recovery.value(
                       QStringLiteral(
                           "command"))
                       .toString()
                << " \""
                << project
                << '"';
            const QString expectedRevision =
                recovery.value(
                            QStringLiteral(
                                "expected_revision"))
                    .toString();
            if (!expectedRevision.isEmpty()) {
                error
                    << " --expect "
                    << expectedRevision;
            }
            error << '\n';
        };
    if (!response.error.isEmpty()) {
        const bool renderCommandResult =
            response.result.isObject() &&
            ((response.command ==
                  QStringLiteral("status") &&
              response.result.toObject()
                  .contains(
                      QStringLiteral(
                          "output_count"))) ||
             (response.command ==
                  QStringLiteral("diff") &&
              response.result.toObject()
                  .contains(
                      QStringLiteral(
                          "change_count"))));
        if (!renderCommandResult) {
            error << response.error << '\n';
            if (!response.usage.isEmpty()) {
                if (response.usage.contains(
                        QLatin1Char('\n'))) {
                    error
                        << '\n'
                        << response.usage;
                    if (!response.usage.endsWith(
                            QLatin1Char('\n'))) {
                        error << '\n';
                    }
                } else {
                    error
                        << "Usage: "
                        << response.usage
                        << '\n';
                }
            }
            writeRecovery();
            error.flush();
            return;
        }
        error.flush();
    }
    if (response.command ==
            QStringLiteral("init")) {
        const QJsonObject result =
            response.result.toObject();
        if (result.value(
                    QStringLiteral(
                        "saved"))
                .toBool()) {
            output
                << "Initialized: "
                << response.project
                << '\n'
                << "Workspace: "
                << result.value(
                       QStringLiteral(
                           "workspace_name"))
                       .toString()
                << " ["
                << result.value(
                       QStringLiteral(
                           "workspace_id"))
                       .toString()
                << "]\n"
                << "Revision: "
                << response.revision
                << '\n'
                << "Outputs: "
                << (result.value(
                        QStringLiteral(
                            "generated"))
                            .toBool()
                        ? "generated"
                        : "not generated")
                << '\n';
        }
    } else if (
        response.command ==
            QStringLiteral("help")) {
        const QJsonObject result =
            response.result.toObject();
        const QString target =
            result.value(
                      QStringLiteral(
                          "target_command"))
                .toString();
        if (target.isEmpty()) {
            output
                << result.value(
                           QStringLiteral(
                               "usage"))
                       .toString();
        } else {
            output
                << "Usage: "
                << result.value(
                           QStringLiteral(
                               "usage"))
                       .toString()
                << '\n'
                << "Project access: "
                << result.value(
                           QStringLiteral(
                               "project_access"))
                       .toString()
                << '\n'
                << "File writes: "
                << result.value(
                           QStringLiteral(
                               "file_write_behavior"))
                       .toString()
                << '\n';
            const QJsonObject
                argumentSchema =
                    result.value(
                              QStringLiteral(
                                  "argument_schema"))
                        .toObject();
            const QJsonArray positionals =
                argumentSchema.value(
                                  QStringLiteral(
                                      "positionals"))
                    .toArray();
            if (!positionals.isEmpty()) {
                output << "Arguments:\n";
                for (const QJsonValue& value :
                     positionals) {
                    const QJsonObject argument =
                        value.toObject();
                    output
                        << "  "
                        << argument.value(
                               QStringLiteral(
                                   "name"))
                               .toString()
                        << " ("
                        << argument.value(
                               QStringLiteral(
                                   "value_type"))
                               .toString()
                        << ", "
                        << (argument.value(
                                    QStringLiteral(
                                        "required"))
                                    .toBool()
                                ? "required"
                                : "optional")
                        << ")\n";
                }
            }
            const QJsonArray options =
                argumentSchema.value(
                                  QStringLiteral(
                                      "options"))
                    .toArray();
            if (!options.isEmpty()) {
                output << "Options:\n";
                for (const QJsonValue& value :
                     options) {
                    const QJsonObject option =
                        value.toObject();
                    output
                        << "  "
                        << option.value(
                               QStringLiteral(
                                   "name"))
                               .toString();
                    const QString valueName =
                        option.value(
                                  QStringLiteral(
                                      "value_name"))
                            .toString();
                    if (!valueName.isEmpty()) {
                        output
                            << " <"
                            << valueName
                            << '>';
                    }
                    output << '\n';
                }
            }
        }
    } else if (
        response.command ==
            QStringLiteral("validate")) {
        if (response.ok) {
            output
                << "Validation passed: "
                << response.project
                << '\n';
        }
    } else if (
        response.command ==
            QStringLiteral("diff")) {
        const QJsonObject result =
            response.result.toObject();
        const QJsonObject counts =
            result.value(
                      QStringLiteral(
                          "counts"))
                .toObject();
        output
            << "Before: "
            << result.value(
                   QStringLiteral(
                       "before_project"))
                   .toString()
            << '\n'
            << "After: "
            << result.value(
                   QStringLiteral(
                       "after_project"))
                   .toString()
            << '\n'
            << "Revisions: "
            << result.value(
                   QStringLiteral(
                       "before_revision"))
                   .toString()
            << " -> "
            << result.value(
                   QStringLiteral(
                       "after_revision"))
                   .toString()
            << '\n'
            << "Changes: "
            << result.value(
                   QStringLiteral(
                       "change_count"))
                   .toInt()
            << " ("
            << counts.value(
                   QStringLiteral("added"))
                   .toInt()
            << " added, "
            << counts.value(
                   QStringLiteral("removed"))
                   .toInt()
            << " removed, "
            << counts.value(
                   QStringLiteral("modified"))
                   .toInt()
            << " modified)\n";
        if (result.value(
                      QStringLiteral(
                          "kind_filter"))
                .toString() !=
            QStringLiteral("all")) {
            output
                << "Kind: "
                << result.value(
                       QStringLiteral(
                           "kind_filter"))
                       .toString()
                << '\n';
        }
        const auto textValue =
            [](const QJsonValue& value) {
                QJsonArray wrapper;
                wrapper.append(value);
                const QByteArray encoded =
                    QJsonDocument(wrapper)
                        .toJson(
                            QJsonDocument::
                                Compact);
                return QString::fromUtf8(
                    encoded.mid(
                        1,
                        encoded.size() -
                            2));
            };
        for (const QJsonValue& value :
             result.value(
                       QStringLiteral(
                           "changes"))
                 .toArray()) {
            const QJsonObject change =
                value.toObject();
            output
                << change.value(
                       QStringLiteral("change"))
                       .toString()
                << '\t'
                << change.value(
                       QStringLiteral(
                           "object_kind"))
                       .toString()
                << '\t'
                << change.value(
                       QStringLiteral("id"))
                       .toString()
                << '\t'
                << change.value(
                       QStringLiteral("name"))
                       .toString()
                << '\t'
                << change.value(
                       QStringLiteral("summary"))
                       .toString()
                << '\n';
            for (const QJsonValue&
                     propertyValue :
                 change.value(
                           QStringLiteral(
                               "property_changes"))
                     .toArray()) {
                const QJsonObject property =
                    propertyValue.toObject();
                output
                    << "  "
                    << property.value(
                           QStringLiteral(
                               "property"))
                           .toString()
                    << ": "
                    << (property.value(
                                     QStringLiteral(
                                         "before_present"))
                                .toBool()
                            ? textValue(
                                  property.value(
                                      QStringLiteral(
                                          "before")))
                            : QStringLiteral(
                                  "<absent>"))
                    << " -> "
                    << (property.value(
                                     QStringLiteral(
                                         "after_present"))
                                .toBool()
                            ? textValue(
                                  property.value(
                                      QStringLiteral(
                                          "after")))
                            : QStringLiteral(
                                  "<absent>"))
                    << '\n';
            }
        }
        const qint64 offset =
            response.resultMetadata
                .value(
                    QStringLiteral(
                        "offset"))
                .toInteger();
        const bool hasMore =
            response.resultMetadata
                .value(
                    QStringLiteral(
                        "has_more"))
                .toBool();
        if (offset > 0 || hasMore) {
            output
                << "Showing "
                << response.resultMetadata
                       .value(
                           QStringLiteral(
                               "returned_count"))
                       .toInteger()
                << " of "
                << response.resultMetadata
                       .value(
                           QStringLiteral(
                               "total_count"))
                       .toInteger()
                << " changes from offset "
                << offset;
            if (hasMore) {
                output
                    << "; use --offset "
                    << response
                           .resultMetadata
                           .value(
                               QStringLiteral(
                                   "next_offset"))
                           .toInteger()
                    << " to continue";
            }
            output << ".\n";
        }
    } else if (
        response.command ==
            QStringLiteral("summary")) {
        const QJsonObject result =
            response.result.toObject();
        const QJsonObject counts =
            result.value(
                      QStringLiteral(
                          "counts"))
                .toObject();
        output
            << result.value(
                   QStringLiteral(
                       "workspace_name"))
                   .toString()
            << " ["
            << result.value(
                   QStringLiteral(
                       "workspace_id"))
                   .toString()
            << "]\n"
            << "Project: "
            << response.project << '\n'
            << "Revision: "
            << response.revision << '\n'
            << "Pages "
            << counts.value(
                   QStringLiteral("pages"))
                   .toInt()
            << ", Blocks "
            << counts.value(
                   QStringLiteral("blocks"))
                   .toInt()
            << ", Registers "
            << counts.value(
                   QStringLiteral(
                       "registers"))
                   .toInt()
            << ", Fields "
            << counts.value(
                   QStringLiteral("fields"))
                   .toInt()
            << ", Enum values "
            << counts.value(
                   QStringLiteral(
                       "enum_values"))
                   .toInt()
            << ", Tags "
            << counts.value(
                   QStringLiteral("tags"))
                   .toInt()
            << '\n';
    } else if (
        response.command ==
            QStringLiteral("status")) {
        const QJsonObject result =
            response.result.toObject();
        output
            << "Outputs: "
            << result.value(
                   QStringLiteral(
                       "synchronized_count"))
                   .toInt()
            << '/'
            << result.value(
                   QStringLiteral(
                       "output_count"))
                   .toInt()
            << " synchronized\n";
        for (const auto& value :
             result.value(
                       QStringLiteral(
                           "artifacts"))
                 .toArray()) {
            const QJsonObject artifact =
                value.toObject();
            output
                << "  "
                << artifact.value(
                       QStringLiteral("state"))
                       .toString()
                << '\t'
                << artifact.value(
                       QStringLiteral("kind"))
                       .toString()
                << '\t'
                << artifact.value(
                       QStringLiteral("path"))
                       .toString()
                << '\n';
        }
    } else if (
        response.command ==
            QStringLiteral("list") ||
        response.command ==
            QStringLiteral("find")) {
        for (const auto& value :
             response.result.toArray()) {
            const QJsonObject object =
                value.toObject();
            output
                << object.value(
                       QStringLiteral("kind"))
                       .toString()
                << '\t'
                << object.value(
                       QStringLiteral("id"))
                       .toString()
                << '\t'
                << object.value(
                       QStringLiteral("path"))
                       .toString();
            if (response.command ==
                QStringLiteral("find")) {
                output
                    << '\t'
                    << object.value(
                           QStringLiteral(
                               "match_field"))
                           .toString()
                    << '\t'
                    << object.value(
                           QStringLiteral(
                               "match_rank"))
                           .toInt();
            }
            output << '\n';
        }
        const qint64 offset =
            response.resultMetadata
                .value(
                    QStringLiteral(
                        "offset"))
                .toInteger();
        const bool hasMore =
            response.resultMetadata
                .value(
                    QStringLiteral(
                        "has_more"))
                .toBool();
        if (offset > 0 || hasMore) {
            output
                << "Showing "
                << response.resultMetadata
                       .value(
                           QStringLiteral(
                               "returned_count"))
                       .toInteger()
                << " of "
                << response.resultMetadata
                       .value(
                           QStringLiteral(
                               "total_count"))
                       .toInteger()
                << (response.command ==
                            QStringLiteral(
                                "find")
                        ? " matches"
                        : " objects")
                << " from offset "
                << offset;
            if (hasMore) {
                output
                    << "; use --offset "
                    << response
                           .resultMetadata
                           .value(
                               QStringLiteral(
                                   "next_offset"))
                           .toInteger()
                    << " to continue";
            }
            output << ".\n";
        }
    } else if (
        response.command ==
            QStringLiteral("get") ||
        response.command ==
            QStringLiteral("get-many") ||
        response.command ==
            QStringLiteral("schema")) {
        output
            << QJsonDocument(
                   response.result
                       .toObject())
                   .toJson(
                       QJsonDocument::
                           Indented);
    } else if (
        response.command ==
        QStringLiteral("generate")) {
        const QJsonObject result =
            response.result.toObject();
        output
            << (result.value(
                    QStringLiteral(
                        "dry_run"))
                        .toBool()
                    ? "Generation preview"
                    : "Generated outputs")
            << ":\n";
        for (const auto& value :
             result.value(
                       QStringLiteral(
                           "artifacts"))
                 .toArray()) {
            const QJsonObject artifact =
                value.toObject();
            output
                << "  "
                << artifact.value(
                       QStringLiteral("kind"))
                       .toString()
                << '\t'
                << artifact.value(
                       QStringLiteral("path"))
                       .toString()
                << '\n';
        }
    } else if (
        response.command ==
        QStringLiteral("apply")) {
        const QJsonObject result =
            response.result.toObject();
        output
            << (result.value(
                    QStringLiteral(
                        "dry_run"))
                        .toBool()
                    ? "Patch preview"
                    : "Patch result")
            << ": "
            << result.value(
                   QStringLiteral(
                       "changes"))
                   .toArray()
                   .size()
            << " operation(s), changed="
            << (result.value(
                    QStringLiteral(
                        "changed"))
                        .toBool()
                    ? "yes"
                    : "no")
            << ", saved="
            << (result.value(
                    QStringLiteral(
                        "saved"))
                        .toBool()
                    ? "yes"
                    : "no")
            << ", outputs="
            << (result.value(
                    QStringLiteral(
                        "generated"))
                        .toBool()
                    ? "written"
                    : "not written")
            << '\n'
            << "Revision: "
            << result.value(
                   QStringLiteral(
                       "new_revision"))
                   .toString()
            << '\n';
        const QJsonObject repair =
            result.value(
                      QStringLiteral(
                          "repair"))
                .toObject();
        if (repair.value(
                      QStringLiteral(
                          "baseline_invalid"))
                .toBool()) {
            output
                << "Repair: "
                << repair.value(
                           QStringLiteral(
                               "before_problem_count"))
                       .toInteger()
                << " -> "
                << repair.value(
                           QStringLiteral(
                               "after_problem_count"))
                       .toInteger()
                << " problem(s), resolved="
                << repair.value(
                           QStringLiteral(
                               "resolved_problem_count"))
                       .toInteger()
                << ", introduced="
                << repair.value(
                           QStringLiteral(
                               "introduced_problem_count"))
                       .toInteger()
                << '\n';
        }
    }
    writeRecovery();
    output.flush();
    if (deferDiagnostics) {
        writeDiagnosticsText(
            response.diagnostics,
            error);
    }
    error.flush();
}

[[nodiscard]] CommandResponse usageError(
    QString command,
    QString message,
    const QString& optionSuggestion =
        QString{})
{
    CommandResponse response;
    response.command =
        std::move(command);
    response.error =
        std::move(message);
    response.usage =
        usageForCommand(
            response.command);
    response.exitCode =
        ExitCode::usageError;
    if (!optionSuggestion.isEmpty()) {
        response.result =
            QJsonObject{
                {QStringLiteral(
                     "suggested_option"),
                 optionSuggestion},
            };
    }
    return response;
}

[[nodiscard]] bool validKind(
    const QString& kind)
{
    return kind ==
               QStringLiteral("all") ||
        kind ==
               QStringLiteral(
                   "workspace") ||
        kind ==
               QStringLiteral("page") ||
        kind ==
               QStringLiteral("block") ||
        kind ==
               QStringLiteral(
                   "register") ||
        kind ==
               QStringLiteral("field") ||
        kind ==
               QStringLiteral("enum");
}

[[nodiscard]] bool hasOnlyKeys(
    const QJsonObject& object,
    const std::set<QString>& allowed,
    QString& error)
{
    for (auto iterator =
             object.constBegin();
         iterator != object.constEnd();
         ++iterator) {
        if (!allowed.contains(
                iterator.key())) {
            error =
                QStringLiteral(
                    "Unknown JSON member '%1'.")
                    .arg(
                        iterator.key());
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validRevision(
    const QString& revision)
{
    constexpr qsizetype prefixSize = 7;
    constexpr qsizetype digestSize = 64;
    if (!revision.startsWith(
            QStringLiteral(
                "sha256:")) ||
        revision.size() !=
            prefixSize + digestSize) {
        return false;
    }
    for (const QChar character :
         revision.sliced(prefixSize)) {
        const ushort value =
            character.unicode();
        if (!((value >= '0' &&
               value <= '9') ||
              (value >= 'a' &&
               value <= 'f'))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool optionValueMissing(
    const QStringList& arguments,
    qsizetype optionIndex)
{
    return optionIndex + 1 >=
            arguments.size() ||
        arguments.at(
                     optionIndex + 1)
            .startsWith(
                QStringLiteral("--"));
}

[[nodiscard]] bool parseBoundedSize(
    const QString& text,
    bool allowZero,
    qsizetype& value)
{
    bool valid = false;
    const qlonglong parsed =
        text.toLongLong(&valid);
    if (!valid ||
        parsed < (allowZero ? 0 : 1) ||
        static_cast<
            unsigned long long>(
            parsed) >
            static_cast<
                unsigned long long>(
                std::numeric_limits<
                    qsizetype>::max())) {
        return false;
    }
    value =
        static_cast<qsizetype>(
            parsed);
    return true;
}

[[nodiscard]] QJsonObject paginationMetadata(
    qsizetype totalCount,
    qsizetype requestedOffset,
    qsizetype returnedCount,
    const std::optional<qsizetype>&
        limit)
{
    const qsizetype start =
        std::min(
            requestedOffset,
            totalCount);
    const bool hasMore =
        start + returnedCount <
        totalCount;
    return {
        {QStringLiteral(
             "total_count"),
         static_cast<qint64>(
             totalCount)},
        {QStringLiteral(
             "offset"),
         static_cast<qint64>(
             requestedOffset)},
        {QStringLiteral(
             "returned_count"),
         static_cast<qint64>(
             returnedCount)},
        {QStringLiteral(
             "truncated"),
         hasMore},
        {QStringLiteral(
             "has_more"),
         hasMore},
        {QStringLiteral(
             "next_offset"),
         hasMore
             ? QJsonValue(
                   static_cast<qint64>(
                       start +
                       returnedCount))
             : QJsonValue(
                   QJsonValue::Null)},
        {QStringLiteral("limit"),
         limit
             ? QJsonValue(
                   static_cast<qint64>(
                       *limit))
             : QJsonValue(
                   QJsonValue::Null)},
    };
}

[[nodiscard]] QString defaultProjectName(
    const QString& projectPath)
{
    QString result =
        QFileInfo(projectPath)
            .completeBaseName();
    if (result.endsWith(
            QStringLiteral(
                ".regmap"),
            Qt::CaseInsensitive)) {
        result.chop(7);
    }
    result = result.trimmed();
    return result.isEmpty()
        ? QStringLiteral(
              "Register Map")
        : result;
}

[[nodiscard]] bool parseDiffOptions(
    const QStringList& arguments,
    DiffOptions& options,
    QString& error,
    QString& optionSuggestion)
{
    optionSuggestion.clear();
    if (arguments.size() < 3) {
        error =
            QStringLiteral(
                "diff requires exactly two project paths before its options.");
        return false;
    }
    options.beforeProjectPath =
        arguments.at(1);
    options.afterProjectPath =
        arguments.at(2);
    bool sawKind = false;
    bool sawOffset = false;
    bool sawLimit = false;
    bool sawExpectBefore = false;
    bool sawExpectAfter = false;
    bool sawRequireEqual = false;
    for (qsizetype index = 3;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral("--kind")) {
            if (sawKind) {
                error =
                    QStringLiteral(
                        "--kind can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--kind requires a value.");
                return false;
            }
            sawKind = true;
            options.kind =
                arguments.at(++index)
                    .toLower();
            if (!validKind(
                    options.kind)) {
                error =
                    QStringLiteral(
                        "Unsupported object kind '%1'. Expected workspace, page, block, register, field, enum, or all.")
                        .arg(
                            options.kind);
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral("--offset")) {
            if (sawOffset) {
                error =
                    QStringLiteral(
                        "--offset can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--offset requires a value.");
                return false;
            }
            sawOffset = true;
            if (!parseBoundedSize(
                    arguments.at(++index),
                    true,
                    options.offset)) {
                error =
                    QStringLiteral(
                        "--offset must be a non-negative integer.");
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral("--limit")) {
            if (sawLimit) {
                error =
                    QStringLiteral(
                        "--limit can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--limit requires a value.");
                return false;
            }
            sawLimit = true;
            qsizetype parsedLimit = 0;
            if (!parseBoundedSize(
                    arguments.at(++index),
                    false,
                    parsedLimit)) {
                error =
                    QStringLiteral(
                        "--limit must be a positive integer.");
                return false;
            }
            options.limit =
                parsedLimit;
            continue;
        }
        if (token ==
            QStringLiteral(
                "--expect-before")) {
            if (sawExpectBefore) {
                error =
                    QStringLiteral(
                        "--expect-before can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--expect-before requires a revision.");
                return false;
            }
            sawExpectBefore = true;
            options.expectedBeforeRevision =
                arguments.at(++index);
            if (!validRevision(
                    options
                        .expectedBeforeRevision)) {
                error =
                    QStringLiteral(
                        "--expect-before must be a lowercase sha256:<64 hex digits> revision.");
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--expect-after")) {
            if (sawExpectAfter) {
                error =
                    QStringLiteral(
                        "--expect-after can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--expect-after requires a revision.");
                return false;
            }
            sawExpectAfter = true;
            options.expectedAfterRevision =
                arguments.at(++index);
            if (!validRevision(
                    options
                        .expectedAfterRevision)) {
                error =
                    QStringLiteral(
                        "--expect-after must be a lowercase sha256:<64 hex digits> revision.");
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--require-equal")) {
            if (sawRequireEqual) {
                error =
                    QStringLiteral(
                        "--require-equal can be specified only once.");
                return false;
            }
            sawRequireEqual = true;
            options.requireEqual = true;
            continue;
        }
        error =
            unknownOptionMessage(
                QStringLiteral("diff"),
                token);
        optionSuggestion =
            suggestedOption(
                QStringLiteral("diff"),
                token);
        return false;
    }
    return true;
}

[[nodiscard]] bool parseStatusOptions(
    const QStringList& arguments,
    StatusOptions& options,
    QString& error,
    QString& optionSuggestion)
{
    optionSuggestion.clear();
    if (arguments.size() < 2) {
        error =
            QStringLiteral(
                "status requires a project path.");
        return false;
    }
    options.projectPath =
        arguments.at(1);
    bool sawRequireCurrent = false;
    for (qsizetype index = 2;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral("--target")) {
            if (optionValueMissing(
                    arguments,
                    index)) {
                error = QStringLiteral(
                    "--target requires xlsx, c-header, or markdown.");
                return false;
            }
            const QString value =
                arguments.at(++index);
            const auto kind =
                parseGenerationTarget(value);
            if (!kind) {
                error = QStringLiteral(
                    "Unsupported generation target '%1'. Expected xlsx, c-header, or markdown.")
                    .arg(value);
                return false;
            }
            if (!options.targets.insert(*kind).second) {
                error = QStringLiteral(
                    "Generation target '%1' can be specified only once.")
                    .arg(
                        generationTargetToken(*kind));
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--require-current")) {
            if (sawRequireCurrent) {
                error =
                    QStringLiteral(
                        "--require-current can be specified only once.");
                return false;
            }
            sawRequireCurrent = true;
            options.requireCurrent = true;
            continue;
        }
        error =
            unknownOptionMessage(
                QStringLiteral(
                    "status"),
                token);
        optionSuggestion =
            suggestedOption(
                QStringLiteral(
                    "status"),
                token);
        return false;
    }
    return true;
}

[[nodiscard]] bool parseGenerateOptions(
    const QStringList& arguments,
    GenerateOptions& options,
    QString& error,
    QString& optionSuggestion)
{
    optionSuggestion.clear();
    if (arguments.size() < 2) {
        error =
            QStringLiteral(
                "generate requires a project path.");
        return false;
    }
    options.projectPath =
        arguments.at(1);
    bool sawDryRun = false;
    bool sawExpect = false;
    for (qsizetype index = 2;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral("--target")) {
            if (optionValueMissing(
                    arguments,
                    index)) {
                error = QStringLiteral(
                    "--target requires xlsx, c-header, or markdown.");
                return false;
            }
            const QString value =
                arguments.at(++index);
            const auto kind =
                parseGenerationTarget(value);
            if (!kind) {
                error = QStringLiteral(
                    "Unsupported generation target '%1'. Expected xlsx, c-header, or markdown.")
                    .arg(value);
                return false;
            }
            if (!options.targets.insert(*kind).second) {
                error = QStringLiteral(
                    "Generation target '%1' can be specified only once.")
                    .arg(
                        generationTargetToken(*kind));
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--dry-run")) {
            if (sawDryRun) {
                error =
                    QStringLiteral(
                        "--dry-run can be specified only once.");
                return false;
            }
            sawDryRun = true;
            options.dryRun = true;
            continue;
        }
        if (token ==
            QStringLiteral("--expect")) {
            if (sawExpect) {
                error =
                    QStringLiteral(
                        "--expect can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--expect requires a revision.");
                return false;
            }
            sawExpect = true;
            options.expectedRevision =
                arguments.at(++index);
            if (!validRevision(
                    options
                        .expectedRevision)) {
                error =
                    QStringLiteral(
                        "--expect must be a lowercase sha256:<64 hex digits> revision.");
                return false;
            }
            continue;
        }
        error =
            unknownOptionMessage(
                QStringLiteral(
                    "generate"),
                token);
        optionSuggestion =
            suggestedOption(
                QStringLiteral(
                    "generate"),
                token);
        return false;
    }
    return true;
}

[[nodiscard]] bool parseInitOptions(
    const QStringList& arguments,
    InitOptions& options,
    QString& error,
    QString& optionSuggestion)
{
    optionSuggestion.clear();
    if (arguments.size() < 2) {
        error =
            QStringLiteral(
                "init requires a .regmap.yaml project path.");
        return false;
    }
    options.projectPath =
        arguments.at(1);
    if (!options.projectPath.endsWith(
            QStringLiteral(
                ".regmap.yaml"),
            Qt::CaseInsensitive)) {
        error =
            QStringLiteral(
                "init project path must end with .regmap.yaml.");
        return false;
    }
    bool sawName = false;
    bool sawWorkspaceId = false;
    bool sawOutputDirectory = false;
    bool sawTarget = false;
    bool sawNoGenerate = false;
    for (qsizetype index = 2;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral("--output-dir")) {
            if (sawOutputDirectory) {
                error = QStringLiteral(
                    "--output-dir can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error = QStringLiteral(
                    "--output-dir requires a relative path.");
                return false;
            }
            sawOutputDirectory = true;
            options.outputDirectory =
                arguments.at(++index);
            continue;
        }
        if (token ==
            QStringLiteral("--target")) {
            if (optionValueMissing(
                    arguments,
                    index)) {
                error = QStringLiteral(
                    "--target requires xlsx, c-header, or markdown.");
                return false;
            }
            sawTarget = true;
            const QString value =
                arguments.at(++index);
            const auto kind =
                parseGenerationTarget(value);
            if (!kind) {
                error = QStringLiteral(
                    "Unsupported generation target '%1'. Expected xlsx, c-header, or markdown.")
                    .arg(value);
                return false;
            }
            if (!options.targets.insert(*kind).second) {
                error = QStringLiteral(
                    "Generation target '%1' can be specified only once.")
                    .arg(
                        generationTargetToken(*kind));
                return false;
            }
            continue;
        }
        const auto targetFileKind =
            token == QStringLiteral("--xlsx-file")
            ? std::optional{
                  GenerationTargetKind::xlsx}
            : token == QStringLiteral(
                  "--c-header-file")
                ? std::optional{
                      GenerationTargetKind::cHeader}
                : token == QStringLiteral(
                      "--markdown-file")
                    ? std::optional{
                          GenerationTargetKind::markdown}
                    : std::nullopt;
        if (targetFileKind) {
            if (options.targetFileNames.contains(
                    *targetFileKind)) {
                error = QStringLiteral(
                    "%1 can be specified only once.")
                    .arg(token);
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error = QStringLiteral(
                    "%1 requires a relative file path.")
                    .arg(token);
                return false;
            }
            options.targetFileNames.insert_or_assign(
                *targetFileKind,
                arguments.at(++index));
            continue;
        }
        if (token ==
            QStringLiteral("--name")) {
            if (sawName) {
                error =
                    QStringLiteral(
                        "--name can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--name requires a value.");
                return false;
            }
            sawName = true;
            options.name =
                arguments.at(++index)
                    .trimmed();
            if (options.name.isEmpty() ||
                options.name.startsWith(
                    QStringLiteral("--"))) {
                error =
                    QStringLiteral(
                        "--name requires a non-empty value.");
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--workspace-id")) {
            if (sawWorkspaceId) {
                error =
                    QStringLiteral(
                        "--workspace-id can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--workspace-id requires a value.");
                return false;
            }
            sawWorkspaceId = true;
            options.workspaceId =
                arguments.at(++index);
            if (options.workspaceId
                    .isEmpty() ||
                options.workspaceId
                    .startsWith(
                        QStringLiteral("--"))) {
                error =
                    QStringLiteral(
                        "--workspace-id requires a non-empty value.");
                return false;
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--no-generate")) {
            if (sawNoGenerate) {
                error =
                    QStringLiteral(
                        "--no-generate can be specified only once.");
                return false;
            }
            sawNoGenerate = true;
            options.generate = false;
            continue;
        }
        error =
            unknownOptionMessage(
                QStringLiteral("init"),
                token);
        optionSuggestion =
            suggestedOption(
                QStringLiteral("init"),
                token);
        return false;
    }
    if (options.name.isEmpty()) {
        options.name =
            defaultProjectName(
            options.projectPath);
    }
    if (!options.generate && sawTarget) {
        error = QStringLiteral(
            "--target cannot be combined with --no-generate because no outputs are generated.");
        return false;
    }
    if (!sawTarget) {
        options.targets =
            allGenerationTargets();
    }
    return true;
}

[[nodiscard]] bool parseApplyOptions(
    const QStringList& arguments,
    ApplyOptions& options,
    QString& error,
    QString& optionSuggestion)
{
    optionSuggestion.clear();
    if (arguments.size() < 3) {
        error =
            QStringLiteral(
                "apply requires a project path and patch JSON path, or '-' for standard input.");
        return false;
    }
    options.projectPath =
        arguments.at(1);
    options.patchPath =
        arguments.at(2);
    bool sawDryRun = false;
    bool sawForce = false;
    bool sawExpect = false;
    for (qsizetype index = 3;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral(
                "--dry-run")) {
            if (sawDryRun) {
                error =
                    QStringLiteral(
                        "--dry-run can be specified only once.");
                return false;
            }
            sawDryRun = true;
            options.dryRun = true;
            continue;
        }
        if (token ==
            QStringLiteral("--force")) {
            if (sawForce) {
                error =
                    QStringLiteral(
                        "--force can be specified only once.");
                return false;
            }
            sawForce = true;
            options.force = true;
            continue;
        }
        if (token ==
            QStringLiteral("--expect")) {
            if (sawExpect) {
                error =
                    QStringLiteral(
                        "--expect can be specified only once.");
                return false;
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                error =
                    QStringLiteral(
                        "--expect requires a revision value.");
                return false;
            }
            sawExpect = true;
            options.expectedRevision =
                arguments.at(++index);
            continue;
        }
        error =
            unknownOptionMessage(
                QStringLiteral("apply"),
                token);
        optionSuggestion =
            suggestedOption(
                QStringLiteral("apply"),
                token);
        return false;
    }
    if (options.force &&
        options.dryRun) {
        error =
            QStringLiteral(
                "--force is not meaningful with --dry-run.");
        return false;
    }
    if (options.force &&
        !options.expectedRevision
             .isEmpty()) {
        error =
            QStringLiteral(
                "--force and --expect are mutually exclusive.");
        return false;
    }
    if (!options.expectedRevision
             .isEmpty() &&
        !validRevision(
            options.expectedRevision)) {
        error =
            QStringLiteral(
                "--expect must be a lowercase sha256:<64 hex digits> revision.");
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<QJsonObject>
readPatch(
    const QString& patchPath,
    QTextStream& standardInput,
    QString& error)
{
    QByteArray content;
    if (patchPath ==
        QStringLiteral("-")) {
        content =
            standardInput.readAll()
                .toUtf8();
    } else {
        QFile file(
            QFileInfo(patchPath)
                .absoluteFilePath());
        if (!file.open(
                QIODevice::ReadOnly)) {
            error =
                QStringLiteral(
                    "Cannot open patch file '%1': %2")
                    .arg(
                        file.fileName(),
                        file.errorString());
            return std::nullopt;
        }
        content = file.readAll();
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            content,
            &parseError);
    if (parseError.error !=
        QJsonParseError::NoError) {
        error =
            QStringLiteral(
                "Cannot parse patch JSON at offset %1: %2")
                .arg(
                    parseError.offset)
                .arg(
                    parseError
                        .errorString());
        return std::nullopt;
    }
    if (!document.isObject()) {
        error =
            QStringLiteral(
                "Patch JSON root must be an object.");
        return std::nullopt;
    }
    QJsonObject patch =
        document.object();
    if (!hasOnlyKeys(
            patch,
            {QStringLiteral(
                 "api_version"),
             QStringLiteral(
                 "expected_revision"),
             QStringLiteral(
                 "operations")},
            error)) {
        return std::nullopt;
    }
    const QJsonValue api =
        patch.value(
            QStringLiteral(
                "api_version"));
    if (!api.isDouble() ||
        api.toDouble() != apiVersion) {
        error =
            QStringLiteral(
                "Patch api_version must be %1.")
                .arg(apiVersion);
        return std::nullopt;
    }
    if (patch.contains(
            QStringLiteral(
                "expected_revision"))) {
        const QJsonValue revision =
            patch.value(
                QStringLiteral(
                    "expected_revision"));
        if (!revision.isString() ||
            !validRevision(
                revision.toString())) {
            error =
                QStringLiteral(
                    "Patch expected_revision must be a lowercase sha256:<64 hex digits> string.");
            return std::nullopt;
        }
    }
    const QJsonValue operations =
        patch.value(
            QStringLiteral(
                "operations"));
    if (!operations.isArray() ||
        operations.toArray().isEmpty()) {
        error =
            QStringLiteral(
                "Patch operations must be a non-empty JSON array.");
        return std::nullopt;
    }
    return patch;
}

[[nodiscard]] std::optional<QString>
resolveOperationObjectReference(
    const QJsonValue& value,
    const QJsonArray& priorChanges,
    qsizetype currentIndex,
    std::string_view memberName,
    QString& error)
{
    const QString member =
        fromUtf8(memberName);
    if (value.isString() &&
        !value.toString().isEmpty()) {
        return value.toString();
    }
    if (!value.isObject()) {
        error =
            QStringLiteral(
                "Operation '%1' must be a non-empty stable ID string or an earlier-operation reference object.")
                .arg(member);
        return std::nullopt;
    }

    const QJsonObject reference =
        value.toObject();
    if (reference.size() != 1 ||
        (!reference.contains(
             QStringLiteral("operation")) &&
         !reference.contains(
             QStringLiteral("ref")))) {
        error =
            QStringLiteral(
                "Operation '%1' reference must contain only {'operation': <earlier zero-based index>} or {'ref': <earlier operation ref>}.")
                .arg(member);
        return std::nullopt;
    }
    if (reference.contains(
            QStringLiteral("ref"))) {
        const QJsonValue named =
            reference.value(
                QStringLiteral("ref"));
        if (!named.isString() ||
            named.toString().isEmpty() ||
            named.toString().trimmed() !=
                named.toString()) {
            error =
                QStringLiteral(
                    "Operation '%1' named reference must be a non-empty string without leading or trailing whitespace.")
                    .arg(member);
            return std::nullopt;
        }
        const QString name =
            named.toString();
        for (const QJsonValue& change :
             priorChanges) {
            const QJsonObject prior =
                change.toObject();
            if (prior.value(
                         QStringLiteral(
                             "ref"))
                    .toString() !=
                name) {
                continue;
            }
            const QJsonValue referencedId =
                prior.value(
                    QStringLiteral("id"));
            if (!referencedId.isString() ||
                referencedId.toString()
                    .isEmpty()) {
                error =
                    QStringLiteral(
                        "Operation '%1' named reference '%2' did not produce an object ID.")
                        .arg(member, name);
                return std::nullopt;
            }
            return referencedId.toString();
        }
        error =
            QStringLiteral(
                "Operation '%1' named reference '%2' must identify an earlier operation ref.")
                .arg(member, name);
        return std::nullopt;
    }
    const QJsonValue operation =
        reference.value(
            QStringLiteral("operation"));
    if (!operation.isDouble() ||
        !std::isfinite(
            operation.toDouble()) ||
        std::floor(
            operation.toDouble()) !=
            operation.toDouble() ||
        operation.toDouble() < 0 ||
        operation.toDouble() >=
            static_cast<double>(
                currentIndex)) {
        error =
            QStringLiteral(
                "Operation '%1' reference index must identify an earlier operation.")
                .arg(member);
        return std::nullopt;
    }
    const qsizetype referencedIndex =
        static_cast<qsizetype>(
            operation.toDouble());
    if (referencedIndex < 0 ||
        referencedIndex >=
            priorChanges.size()) {
        error =
            QStringLiteral(
                "Operation '%1' reference index %2 has no prior result.")
                .arg(member)
                .arg(referencedIndex);
        return std::nullopt;
    }
    const QJsonValue referencedId =
        priorChanges.at(
                        referencedIndex)
            .toObject()
            .value(
                QStringLiteral("id"));
    if (!referencedId.isString() ||
        referencedId.toString()
            .isEmpty()) {
        error =
            QStringLiteral(
                "Operation '%1' reference index %2 did not produce an object ID.")
                .arg(member)
                .arg(referencedIndex);
        return std::nullopt;
    }
    return referencedId.toString();
}

[[nodiscard]] bool applyOperations(
    Workspace& candidate,
    const QJsonArray& operations,
    QJsonArray& changes,
    qsizetype& failedIndex,
    QString& error)
{
    for (qsizetype index = 0;
         index < operations.size();
         ++index) {
        failedIndex = index;
        const QJsonValue operationValue =
            operations.at(index);
        if (!operationValue.isObject()) {
            error =
                QStringLiteral(
                    "Operation must be a JSON object.");
            return false;
        }
        const QJsonObject operation =
            operationValue.toObject();
        const QJsonValue opValue =
            operation.value(
                QStringLiteral("op"));
        if (!opValue.isString()) {
            error =
                QStringLiteral(
                    "Operation 'op' must be a string.");
            return false;
        }
        const QString op =
            opValue.toString();
        QString operationReference;
        if (operation.contains(
                QStringLiteral("ref"))) {
            const QJsonValue reference =
                operation.value(
                    QStringLiteral("ref"));
            if (!reference.isString() ||
                reference.toString()
                    .isEmpty() ||
                reference.toString()
                        .trimmed() !=
                    reference.toString()) {
                error =
                    QStringLiteral(
                        "Operation 'ref' must be a non-empty string without leading or trailing whitespace.");
                return false;
            }
            operationReference =
                reference.toString();
            const bool duplicate =
                std::ranges::any_of(
                    changes,
                    [&operationReference](
                        const QJsonValue&
                            change) {
                        return change
                                   .toObject()
                                   .value(
                                       QStringLiteral(
                                           "ref"))
                                   .toString() ==
                            operationReference;
                    });
            if (duplicate) {
                error =
                    QStringLiteral(
                        "Operation 'ref' value '%1' must be unique within the patch.")
                        .arg(
                            operationReference);
                return false;
            }
        }
        const auto appendChange =
            [&changes,
             &operationReference](
                QJsonObject change) {
                if (!operationReference
                         .isEmpty()) {
                    change.insert(
                        QStringLiteral(
                            "ref"),
                        operationReference);
                }
                changes.append(
                    std::move(change));
            };
        if (op ==
            QStringLiteral("set")) {
            if (!hasOnlyKeys(
                    operation,
                    {QStringLiteral("op"),
                     QStringLiteral("ref"),
                     QStringLiteral("id"),
                     QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "value")},
                    error)) {
                return false;
            }
            const QJsonValue idValue =
                operation.value(
                    QStringLiteral("id"));
            const QJsonValue propertyValue =
                operation.value(
                    QStringLiteral(
                        "property"));
            if (!propertyValue.isString() ||
                propertyValue.toString()
                    .isEmpty()) {
                error =
                    QStringLiteral(
                        "Set operation 'property' must be a non-empty string.");
                return false;
            }
            if (!operation.contains(
                    QStringLiteral(
                        "value"))) {
                error =
                    QStringLiteral(
                        "Set operation must contain 'value'; use JSON null to clear an optional property.");
                return false;
            }

            const auto id =
                resolveOperationObjectReference(
                    idValue,
                    changes,
                    index,
                    "id",
                    error);
            if (!id) {
                return false;
            }
            const QString property =
                propertyValue.toString();
            const std::string objectId =
                id->toUtf8()
                    .toStdString();
            const QJsonValue beforeObject =
                findObjectJson(
                    candidate,
                    objectId);
            QString kind;
            if (!setObjectProperty(
                    candidate,
                    *id,
                    property,
                    operation.value(
                        QStringLiteral(
                            "value")),
                    kind,
                    error)) {
                return false;
            }
            const QJsonValue afterObject =
                findObjectJson(
                    candidate,
                    objectId);
            if (!beforeObject.isObject() ||
                !afterObject.isObject()) {
                error =
                    QStringLiteral(
                        "Object '%1' could not be inspected before and after the operation.")
                        .arg(*id);
                return false;
            }
            const QJsonValue before =
                beforeObject.toObject()
                    .value(property);
            const QJsonValue after =
                afterObject.toObject()
                    .value(property);
            if (before.isUndefined() ||
                after.isUndefined()) {
                error =
                    QStringLiteral(
                        "Property '%1' is not represented by the stable CLI schema for %2 '%3'.")
                        .arg(
                            property,
                            kind,
                            *id);
                return false;
            }
            appendChange(
                QJsonObject{
                    {QStringLiteral(
                         "index"),
                     static_cast<qint64>(
                         index)},
                    {QStringLiteral("op"),
                     op},
                    {QStringLiteral("id"),
                     *id},
                    {QStringLiteral("kind"),
                     kind},
                    {QStringLiteral(
                         "property"),
                     property},
                    {QStringLiteral(
                         "before"),
                     before},
                    {QStringLiteral("after"),
                     after},
                    {QStringLiteral(
                         "changed"),
                     before != after},
                });
            continue;
        }

        if (op ==
            QStringLiteral("add")) {
            if (!hasOnlyKeys(
                    operation,
                    {QStringLiteral("op"),
                     QStringLiteral("ref"),
                     QStringLiteral("kind"),
                     QStringLiteral("id"),
                     QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "value")},
                    error)) {
                return false;
            }
            const QJsonValue kindValue =
                operation.value(
                    QStringLiteral("kind"));
            const QJsonValue idValue =
                operation.value(
                    QStringLiteral("id"));
            const QJsonValue parentValue =
                operation.value(
                    QStringLiteral(
                        "parent_id"));
            const QJsonValue value =
                operation.value(
                    QStringLiteral(
                        "value"));
            if (!kindValue.isString() ||
                kindValue.toString()
                    .isEmpty()) {
                error =
                    QStringLiteral(
                        "Add operation 'kind' must be a non-empty string.");
                return false;
            }
            if (!idValue.isString() ||
                idValue.toString()
                    .isEmpty()) {
                error =
                    QStringLiteral(
                        "Add operation 'id' must be a non-empty stable ID string.");
                return false;
            }
            if (!value.isObject()) {
                error =
                    QStringLiteral(
                        "Add operation 'value' must be an object.");
                return false;
            }
            const QString kind =
                kindValue.toString();
            QString id =
                idValue.toString();
            const bool automaticId =
                id ==
                QStringLiteral("auto");
            if (automaticId) {
                id =
                    automaticAddObjectId(
                        candidate,
                        kind,
                        value.toObject()
                            .value(
                                QStringLiteral(
                                    "name"))
                            .toString());
            }
            const auto parentId =
                resolveOperationObjectReference(
                    parentValue,
                    changes,
                    index,
                    "parent_id",
                    error);
            if (!parentId) {
                return false;
            }
            if (!addObject(
                    candidate,
                    kind,
                    id,
                    *parentId,
                    value.toObject(),
                    error)) {
                return false;
            }
            const QJsonValue after =
                findObjectJson(
                    candidate,
                    id.toUtf8()
                        .toStdString());
            if (!after.isObject()) {
                error =
                    QStringLiteral(
                        "New %1 '%2' could not be inspected.")
                        .arg(
                            kind,
                            id);
                return false;
            }
            appendChange(
                QJsonObject{
                    {QStringLiteral(
                         "index"),
                     static_cast<qint64>(
                         index)},
                    {QStringLiteral("op"),
                     op},
                    {QStringLiteral("id"),
                     id},
                    {QStringLiteral("kind"),
                     kind},
                    {QStringLiteral(
                         "parent_id"),
                     *parentId},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "object")},
                    {QStringLiteral(
                         "automatic_id"),
                     automaticId},
                    {QStringLiteral(
                         "before"),
                     QJsonValue(
                         QJsonValue::Null)},
                    {QStringLiteral("after"),
                     after},
                    {QStringLiteral(
                         "changed"),
                     true},
                });
            continue;
        }

        if (op ==
            QStringLiteral("copy")) {
            if (!hasOnlyKeys(
                    operation,
                    {QStringLiteral("op"),
                     QStringLiteral("ref"),
                     QStringLiteral("id"),
                     QStringLiteral(
                         "new_id"),
                     QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "unique_name")},
                    error)) {
                return false;
            }
            const QJsonValue sourceValue =
                operation.value(
                    QStringLiteral("id"));
            const QJsonValue newIdValue =
                operation.value(
                    QStringLiteral(
                        "new_id"));
            const QJsonValue parentValue =
                operation.value(
                    QStringLiteral(
                        "parent_id"));
            if (!newIdValue.isString() ||
                newIdValue.toString()
                    .isEmpty()) {
                error =
                    QStringLiteral(
                        "Copy operation 'new_id' must be a non-empty stable ID string.");
                return false;
            }
            QJsonObject overrides;
            if (operation.contains(
                    QStringLiteral("value"))) {
                const QJsonValue value =
                    operation.value(
                        QStringLiteral(
                            "value"));
                if (!value.isObject()) {
                    error =
                        QStringLiteral(
                            "Copy operation 'value' must be an object when provided.");
                    return false;
                }
                overrides =
                    value.toObject();
            }
            bool uniqueName = false;
            if (operation.contains(
                    QStringLiteral(
                        "unique_name"))) {
                const QJsonValue
                    uniqueNameValue =
                        operation.value(
                            QStringLiteral(
                                "unique_name"));
                if (!uniqueNameValue
                         .isBool()) {
                    error =
                        QStringLiteral(
                            "Copy operation 'unique_name' must be a boolean when present.");
                    return false;
                }
                uniqueName =
                    uniqueNameValue
                        .toBool();
            }

            const auto sourceId =
                resolveOperationObjectReference(
                    sourceValue,
                    changes,
                    index,
                    "id",
                    error);
            if (!sourceId) {
                return false;
            }
            QString newId =
                newIdValue.toString();
            const bool automaticId =
                newId ==
                QStringLiteral("auto");
            if (automaticId) {
                const auto generatedId =
                    automaticCopyRootId(
                        candidate,
                        *sourceId,
                        error);
                if (!generatedId) {
                    return false;
                }
                newId = *generatedId;
            }
            const auto parentId =
                resolveOperationObjectReference(
                    parentValue,
                    changes,
                    index,
                    "parent_id",
                    error);
            if (!parentId) {
                return false;
            }
            QString kind;
            QJsonObject idMapping;
            if (!copyObject(
                    candidate,
                    *sourceId,
                    newId,
                    *parentId,
                    overrides,
                    uniqueName,
                    kind,
                    idMapping,
                    error)) {
                return false;
            }
            const QJsonValue after =
                findObjectJson(
                    candidate,
                    newId.toUtf8()
                        .toStdString());
            if (!after.isObject()) {
                error =
                    QStringLiteral(
                        "Copied %1 '%2' could not be inspected.")
                        .arg(
                            kind,
                            newId);
                return false;
            }
            appendChange(
                QJsonObject{
                    {QStringLiteral(
                         "index"),
                     static_cast<qint64>(
                         index)},
                    {QStringLiteral("op"),
                     op},
                    {QStringLiteral(
                         "source_id"),
                     *sourceId},
                    {QStringLiteral("id"),
                     newId},
                    {QStringLiteral("kind"),
                     kind},
                    {QStringLiteral(
                         "parent_id"),
                     *parentId},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "object")},
                    {QStringLiteral(
                         "id_mapping"),
                     idMapping},
                    {QStringLiteral(
                         "automatic_id"),
                     automaticId},
                    {QStringLiteral(
                         "unique_name"),
                     uniqueName},
                    {QStringLiteral(
                         "before"),
                     QJsonValue(
                         QJsonValue::Null)},
                    {QStringLiteral("after"),
                     after},
                    {QStringLiteral(
                         "changed"),
                     true},
                });
            continue;
        }

        if (op ==
            QStringLiteral("move")) {
            if (!hasOnlyKeys(
                    operation,
                    {QStringLiteral("op"),
                     QStringLiteral("ref"),
                     QStringLiteral("id"),
                     QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "before_id"),
                     QStringLiteral(
                         "placement")},
                    error)) {
                return false;
            }
            const QJsonValue idValue =
                operation.value(
                    QStringLiteral("id"));
            const QJsonValue parentValue =
                operation.value(
                    QStringLiteral(
                        "parent_id"));
            const auto id =
                resolveOperationObjectReference(
                    idValue,
                    changes,
                    index,
                    "id",
                    error);
            if (!id) {
                return false;
            }
            const auto parentId =
                resolveOperationObjectReference(
                    parentValue,
                    changes,
                    index,
                    "parent_id",
                    error);
            if (!parentId) {
                return false;
            }
            std::optional<QString>
                beforeId;
            if (operation.contains(
                    QStringLiteral(
                        "before_id"))) {
                const QJsonValue
                    beforeValue =
                        operation.value(
                            QStringLiteral(
                                "before_id"));
                if (beforeValue.isNull()) {
                    beforeId =
                        QString{};
                } else {
                    const auto
                        resolvedBeforeId =
                            resolveOperationObjectReference(
                                beforeValue,
                                changes,
                                index,
                                "before_id",
                                error);
                    if (!resolvedBeforeId) {
                        return false;
                    }
                    beforeId =
                        *resolvedBeforeId;
                }
            }
            bool automaticPlacement =
                false;
            if (operation.contains(
                    QStringLiteral(
                        "placement"))) {
                const QJsonValue
                    placementValue =
                        operation.value(
                            QStringLiteral(
                                "placement"));
                if (!placementValue
                         .isString() ||
                    placementValue
                            .toString() !=
                        QStringLiteral(
                            "auto")) {
                    error =
                        QStringLiteral(
                            "Move operation 'placement' must be the exact string 'auto' when present.");
                    return false;
                }
                automaticPlacement =
                    true;
            }
            const QJsonObject
                workspaceBefore =
                    workspaceJson(
                        candidate);
            QString kind;
            QString previousParentId;
            bool changed = false;
            if (!moveObject(
                    candidate,
                    *id,
                    *parentId,
                    beforeId,
                    automaticPlacement,
                    kind,
                    previousParentId,
                    changed,
                    error)) {
                return false;
            }
            changed =
                workspaceJson(
                    candidate) !=
                workspaceBefore;
            const auto afterDescriptor =
                describeObject(
                    candidate, *id);
            if (!afterDescriptor) {
                error =
                    QStringLiteral(
                        "Moved %1 '%2' could not be inspected.")
                        .arg(
                            kind, *id);
                return false;
            }
            QJsonValue placementResult(
                QJsonValue::Null);
            if (automaticPlacement ||
                (beforeId.has_value() &&
                 kind ==
                     QStringLiteral(
                         "register"))) {
                const QJsonValue movedObject =
                    findObjectJson(
                        candidate,
                        id->toUtf8()
                            .toStdString());
                if (!movedObject.isObject()) {
                    error =
                        QStringLiteral(
                            "Automatically placed %1 '%2' could not be inspected.")
                            .arg(
                                kind, *id);
                    return false;
                }
                const QJsonObject object =
                    movedObject.toObject();
                if (kind ==
                    QStringLiteral("block")) {
                    placementResult =
                        QJsonObject{
                            {QStringLiteral(
                                 "property"),
                             QStringLiteral(
                                 "base")},
                            {QStringLiteral(
                                 "value"),
                             object.value(
                                 QStringLiteral(
                                     "base"))},
                        };
                } else if (
                    kind ==
                    QStringLiteral(
                        "register")) {
                    placementResult =
                        QJsonObject{
                            {QStringLiteral(
                                 "property"),
                             QStringLiteral(
                                 "offset")},
                            {QStringLiteral(
                                 "value"),
                             object.value(
                                 QStringLiteral(
                                     "offset"))},
                        };
                } else if (
                    kind ==
                    QStringLiteral("field")) {
                    placementResult =
                        QJsonObject{
                            {QStringLiteral(
                                 "property"),
                             QStringLiteral(
                                 "lsb")},
                            {QStringLiteral(
                                 "value"),
                             object.value(
                                 QStringLiteral(
                                     "lsb"))},
                            {QStringLiteral(
                                 "msb"),
                             object.value(
                                 QStringLiteral(
                                     "msb"))},
                        };
                }
            }
            appendChange(
                QJsonObject{
                    {QStringLiteral(
                         "index"),
                     static_cast<qint64>(
                         index)},
                    {QStringLiteral("op"),
                     op},
                    {QStringLiteral("id"),
                     *id},
                    {QStringLiteral("kind"),
                     kind},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "parent_id")},
                    {QStringLiteral(
                         "placement_requested"),
                     beforeId.has_value()},
                    {QStringLiteral(
                         "automatic_placement"),
                     automaticPlacement},
                    {QStringLiteral(
                         "placement_result"),
                     placementResult},
                    {QStringLiteral(
                         "placement_before_id"),
                     beforeId &&
                             !beforeId
                                  ->isEmpty()
                         ? QJsonValue(
                               *beforeId)
                         : QJsonValue(
                               QJsonValue::
                                   Null)},
                    {QStringLiteral(
                         "before"),
                     previousParentId},
                    {QStringLiteral("after"),
                     afterDescriptor
                         ->parentId},
                    {QStringLiteral(
                         "changed"),
                     changed},
                });
            continue;
        }

        if (op ==
            QStringLiteral("remove")) {
            if (!hasOnlyKeys(
                    operation,
                    {QStringLiteral("op"),
                     QStringLiteral("ref"),
                     QStringLiteral("id"),
                     QStringLiteral(
                         "cascade")},
                    error)) {
                return false;
            }
            const QJsonValue idValue =
                operation.value(
                    QStringLiteral("id"));
            bool cascade = false;
            if (operation.contains(
                    QStringLiteral(
                        "cascade")) &&
                !requireBool(
                    operation.value(
                        QStringLiteral(
                            "cascade")),
                    cascade,
                    error)) {
                error.prepend(
                    QStringLiteral(
                        "Invalid remove cascade. "));
                return false;
            }
            const auto id =
                resolveOperationObjectReference(
                    idValue,
                    changes,
                    index,
                    "id",
                    error);
            if (!id) {
                return false;
            }
            const QJsonValue before =
                findObjectJson(
                    candidate,
                    id->toUtf8()
                        .toStdString());
            QString kind;
            std::size_t descendants = 0;
            if (!removeObjectChecked(
                    candidate,
                    *id,
                    cascade,
                    kind,
                    descendants,
                    error)) {
                return false;
            }
            appendChange(
                QJsonObject{
                    {QStringLiteral(
                         "index"),
                     static_cast<qint64>(
                         index)},
                    {QStringLiteral("op"),
                     op},
                    {QStringLiteral("id"),
                     *id},
                    {QStringLiteral("kind"),
                     kind},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "object")},
                    {QStringLiteral(
                         "cascade"),
                     cascade},
                    {QStringLiteral(
                         "descendant_count"),
                     QString::number(
                         descendants)},
                    {QStringLiteral(
                         "before"),
                     before},
                    {QStringLiteral("after"),
                     QJsonValue(
                         QJsonValue::Null)},
                    {QStringLiteral(
                         "changed"),
                     true},
                });
            continue;
        }

        error =
            QStringLiteral(
                "Operation 'op' must be one of: set, add, copy, move, remove.");
        return false;
    }
    failedIndex = -1;
    return true;
}

[[nodiscard]] bool hasChanged(
    const QJsonArray& changes)
{
    return std::ranges::any_of(
        changes,
        [](const QJsonValue& value) {
            return value.toObject()
                .value(
                    QStringLiteral(
                        "changed"))
                .toBool();
        });
}

[[nodiscard]] QJsonObject applyResult(
    bool dryRun,
    bool changed,
    bool saved,
    bool generated,
    bool generationPreviewed,
    const QString& previousRevision,
    const QString& newRevision,
    const QJsonArray& changes,
    const QJsonArray& artifacts,
    const QJsonObject& repair = {})
{
    QJsonObject result{
        {QStringLiteral("dry_run"),
         dryRun},
        {QStringLiteral("changed"),
         changed},
        {QStringLiteral("saved"),
         saved},
        {QStringLiteral("generated"),
         generated},
        {QStringLiteral(
             "outputs_written"),
         generated},
        {QStringLiteral(
             "outputs_current"),
         generated
             ? QJsonValue(true)
             : saved
                 ? QJsonValue(false)
                 : QJsonValue(
                       QJsonValue::Null)},
        {QStringLiteral(
             "generation_previewed"),
         generationPreviewed},
        {QStringLiteral(
             "previous_revision"),
         previousRevision},
        {QStringLiteral(
             "new_revision"),
         newRevision},
        {QStringLiteral("changes"),
         changes},
        {QStringLiteral("artifacts"),
         artifacts},
    };
    if (!repair.isEmpty()) {
        result.insert(
            QStringLiteral("repair"),
            repair);
    }
    return result;
}

[[nodiscard]] QJsonObject
operationFailureJson(
    qsizetype failedIndex,
    const QJsonArray& operations,
    const QJsonArray& completedChanges,
    const QString& message)
{
    const bool validIndex =
        failedIndex >= 0 &&
        failedIndex < operations.size();
    const QJsonValue operation =
        validIndex
        ? operations.at(failedIndex)
        : QJsonValue(
              QJsonValue::Null);
    return {
        {QStringLiteral("stage"),
         QStringLiteral("operation")},
        {QStringLiteral(
             "operation_index"),
         validIndex
             ? QJsonValue(
                   static_cast<qint64>(
                       failedIndex))
             : QJsonValue(
                   QJsonValue::Null)},
        {QStringLiteral(
             "json_pointer"),
         validIndex
             ? QJsonValue(
                   QStringLiteral(
                       "/operations/%1")
                       .arg(
                           failedIndex))
             : QJsonValue(
                   QJsonValue::Null)},
        {QStringLiteral(
             "operation_count"),
         static_cast<qint64>(
             operations.size())},
        {QStringLiteral(
             "completed_operation_count"),
         static_cast<qint64>(
             completedChanges.size())},
        {QStringLiteral(
             "operation"),
         operation},
        {QStringLiteral("message"),
         message},
        {QStringLiteral(
             "writes_performed"),
         false},
        {QStringLiteral(
             "completed_operations_rolled_back"),
         true},
    };
}

[[nodiscard]] QJsonObject
prewriteFailureJson(
    const QString& stage,
    const QJsonArray& operations,
    const QJsonArray& completedChanges,
    const std::vector<Diagnostic>&
        diagnostics,
    const QString& message)
{
    QJsonArray diagnosticIndexes;
    QJsonArray errorCodes;
    QJsonArray objectIds;
    for (qsizetype index = 0;
         index <
         static_cast<qsizetype>(
             diagnostics.size());
         ++index) {
        const Diagnostic& diagnostic =
            diagnostics.at(
                static_cast<std::size_t>(
                    index));
        if (diagnostic.severity !=
            DiagnosticSeverity::error) {
            continue;
        }
        diagnosticIndexes.append(
            static_cast<qint64>(index));
        const QJsonValue code(
            fromUtf8(
                diagnostic.code));
        if (!errorCodes.contains(code)) {
            errorCodes.append(code);
        }
        if (!diagnostic.objectId.empty()) {
            const QJsonValue objectId(
                fromUtf8(
                    diagnostic.objectId));
            if (!objectIds.contains(
                    objectId)) {
                objectIds.append(
                    objectId);
            }
        }
    }
    return {
        {QStringLiteral("stage"),
         stage},
        {QStringLiteral(
             "operation_index"),
         QJsonValue(
             QJsonValue::Null)},
        {QStringLiteral(
             "json_pointer"),
         QJsonValue(
             QJsonValue::Null)},
        {QStringLiteral(
             "operation_count"),
         static_cast<qint64>(
             operations.size())},
        {QStringLiteral(
             "completed_operation_count"),
         static_cast<qint64>(
             completedChanges.size())},
        {QStringLiteral(
             "operation"),
         QJsonValue(
             QJsonValue::Null)},
        {QStringLiteral("message"),
         message},
        {QStringLiteral(
             "error_diagnostic_indexes"),
         diagnosticIndexes},
        {QStringLiteral(
             "error_codes"),
         errorCodes},
        {QStringLiteral(
             "object_ids"),
         objectIds},
        {QStringLiteral(
             "writes_performed"),
         false},
        {QStringLiteral(
             "completed_operations_rolled_back"),
         true},
    };
}

void addGenerationRecovery(
    QJsonObject& result,
    const QString& project,
    const QString& reason,
    const QString& expectedRevision =
        QString{},
    const GenerationTargetSet& targets = {})
{
    QJsonArray arguments{
        QStringLiteral(
            "generate"),
        project,
    };
    QJsonObject recovery{
        {QStringLiteral(
             "command"),
         QStringLiteral(
             "generate")},
        {QStringLiteral(
             "project"),
         project},
        {QStringLiteral(
             "reason"),
         reason},
    };
    if (!expectedRevision.isEmpty()) {
        recovery.insert(
            QStringLiteral(
                "expected_revision"),
            expectedRevision);
        arguments.append(
            QStringLiteral(
                "--expect"));
        arguments.append(
            expectedRevision);
    }
    for (const QString& token :
         generationTargetArguments(
             targets)) {
        arguments.append(token);
    }
    recovery.insert(
        QStringLiteral(
            "arguments"),
        arguments);
    result.insert(
        QStringLiteral("recovery"),
        recovery);
}

[[nodiscard]] CommandResponse runSchema()
{
    CommandResponse response;
    response.command =
        QStringLiteral("schema");
    response.ok = true;
    response.result = schemaJson();
    return response;
}

[[nodiscard]] CommandResponse runVersion()
{
    CommandResponse response;
    response.command =
        QStringLiteral("version");
    response.ok = true;
    response.result =
        QJsonObject{
            {QStringLiteral(
                 "cli_version"),
             fromUtf8(cliVersion)},
            {QStringLiteral(
                 "api_version"),
             apiVersion},
            {QStringLiteral(
                 "project_schema"),
             projectSchemaJson()},
            {QStringLiteral(
                 "json_transport"),
             jsonTransportContract()},
            {QStringLiteral(
                 "capabilities_command"),
             QStringLiteral("schema")},
            {QStringLiteral(
                 "help_option"),
             QStringLiteral("--help")},
            {QStringLiteral(
                 "help_command"),
             QStringLiteral("help")},
        };
    return response;
}

[[nodiscard]] CommandResponse runHelp(
    QString targetCommand = {})
{
    CommandResponse response;
    response.command =
        QStringLiteral("help");
    targetCommand =
        targetCommand.trimmed()
            .toLower();
    if (!targetCommand.isEmpty()) {
        const QJsonObject schema =
            schemaJson();
        const QJsonArray commands =
            schema.value(
                      QStringLiteral(
                          "commands"))
                .toArray();
        const auto descriptor =
            std::ranges::find_if(
                commands,
                [&targetCommand](
                    const QJsonValue& value) {
                    const QJsonObject command =
                        value.toObject();
                    if (command
                            .value(
                                QStringLiteral(
                                    "name"))
                            .toString() ==
                        targetCommand) {
                        return true;
                    }
                    return command
                        .value(
                            QStringLiteral(
                                "aliases"))
                        .toArray()
                        .contains(
                            targetCommand);
                });
        if (descriptor ==
            commands.end()) {
            const QString suggestion =
                suggestedCommand(
                    targetCommand);
            response.error =
                suggestion.isEmpty()
                ? QStringLiteral(
                      "Unknown help topic '%1'.")
                      .arg(
                          targetCommand)
                : QStringLiteral(
                      "Unknown help topic '%1'. Did you mean '%2'?")
                      .arg(
                          targetCommand,
                          suggestion);
            response.usage =
                usageForCommand(
                    QStringLiteral(
                        "help"));
            response.exitCode =
                ExitCode::usageError;
            if (!suggestion.isEmpty()) {
                response.result =
                    QJsonObject{
                        {QStringLiteral(
                             "suggested_command"),
                         suggestion},
                    };
            }
            return response;
        }
        const QJsonObject command =
            descriptor->toObject();
        const QString canonicalCommand =
            command.value(
                       QStringLiteral(
                           "name"))
                .toString();
        QJsonObject result{
            {QStringLiteral(
                 "target_command"),
             canonicalCommand},
            {QStringLiteral("usage"),
             command.value(
                 QStringLiteral(
                     "usage"))},
            {QStringLiteral(
                 "project_access"),
             command.value(
                 QStringLiteral(
                     "project_access"))},
            {QStringLiteral(
                 "file_write_behavior"),
             command.value(
                 QStringLiteral(
                     "file_write_behavior"))},
            {QStringLiteral(
                 "argument_schema"),
             schema.value(
                       QStringLiteral(
                           "command_argument_schemas"))
                 .toObject()
                 .value(
                     canonicalCommand)},
        };
        if (command.contains(
                QStringLiteral(
                    "aliases"))) {
            result.insert(
                QStringLiteral(
                    "aliases"),
                command.value(
                    QStringLiteral(
                        "aliases")));
        }
        response.ok = true;
        response.result =
            std::move(result);
        return response;
    }
    response.ok = true;
    QJsonObject result =
        schemaJson();
    result.insert(
        QStringLiteral(
            "usage"),
        usageText());
    response.result =
        result;
    return response;
}

[[nodiscard]] CommandResponse runInit(
    const QStringList& arguments)
{
    InitOptions options;
    QString optionErrorText;
    QString optionSuggestion;
    if (!parseInitOptions(
            arguments,
            options,
            optionErrorText,
            optionSuggestion)) {
        return usageError(
            QStringLiteral("init"),
            optionErrorText,
            optionSuggestion);
    }

    CommandResponse response;
    response.command =
        QStringLiteral("init");
    response.project =
        QFileInfo(options.projectPath)
            .absoluteFilePath();
    if (QFileInfo::exists(
            response.project)) {
        response.error =
            QStringLiteral(
                "Refusing to overwrite existing project '%1'.")
                .arg(
                    response.project);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }

    std::optional<ObjectId>
        requestedWorkspaceId;
    if (!options.workspaceId
             .isEmpty()) {
        requestedWorkspaceId =
            options.workspaceId
                .toUtf8()
                .toStdString();
    }
    NewProject project =
        makeDefaultProject(
            toPath(response.project),
            options.name.toUtf8()
                .toStdString(),
            std::move(
                requestedWorkspaceId));
    QString generationConfigurationError;
    if (!configureInitialGeneration(
            project.manifest,
            options.outputDirectory,
            options.targets,
            options.targetFileNames,
            generationConfigurationError)) {
        return usageError(
            QStringLiteral("init"),
            generationConfigurationError);
    }
    ProjectManifest generationManifest =
        manifestForTargets(
            project.manifest,
            options.targets);
    response.diagnostics =
        validateWorkspace(
            project.workspace);
    if (hasErrors(
            response.diagnostics)) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }

    const auto existingOutput =
        [&]() -> QString {
            if (!options.generate) {
                return {};
            }
            for (const auto& target :
                 generationManifest.targets) {
                const QString path =
                    fromPath(
                        target.path
                            .resolved);
                if (QFileInfo::exists(
                        path)) {
                    return path;
                }
            }
            return {};
        };
    GenerationResult generation;
    QJsonArray artifacts;
    if (options.generate) {
        const QString occupied =
            existingOutput();
        if (!occupied.isEmpty()) {
            response.error =
                QStringLiteral(
                    "Refusing to overwrite existing generated output '%1'. Use a different project directory or --no-generate.")
                    .arg(occupied);
            response.exitCode =
                ExitCode::writeError;
            return response;
        }
        generation =
            generateArtifacts(
                project.workspace,
                generationManifest);
        appendUniqueDiagnostics(
            response.diagnostics,
            std::move(
                generation.diagnostics));
        artifacts =
            artifactPreviewJson(
                generation.artifacts);
        if (hasErrors(
                response.diagnostics)) {
            response.error =
                QStringLiteral(
                    "Initial artifact generation failed; no files were written.");
            response.exitCode =
                ExitCode::generationError;
            return response;
        }
    }

    if (QFileInfo::exists(
            response.project)) {
        response.error =
            QStringLiteral(
                "Project appeared while init was preparing it; no existing file was overwritten.");
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    const QString occupied =
        existingOutput();
    if (!occupied.isEmpty()) {
        response.error =
            QStringLiteral(
                "Generated output '%1' appeared while init was preparing the project; no existing file was overwritten.")
                .arg(occupied);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    appendUniqueDiagnostics(
        response.diagnostics,
        saveProjectFile(
            project.manifest,
            project.workspace));
    if (hasErrors(
            response.diagnostics)) {
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    response.revision =
        fileRevision(
            response.project);
    if (response.revision.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Project was created, but its revision could not be calculated.",
                response.project));
        response.exitCode =
            ExitCode::writeError;
        return response;
    }

    OpenedProject verification =
        open(response.project);
    appendUniqueDiagnostics(
        response.diagnostics,
        std::move(
            verification.open
                .diagnostics));
    if (verification.revision !=
        response.revision) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionConflictCode,
                "Project changed immediately after creation; generated outputs were not written.",
                response.project));
        response.error =
            QStringLiteral(
                "Project changed immediately after creation; inspect the saved project before retrying.");
        response.revision =
            verification.revision;
        response.result =
            QJsonObject{
                {QStringLiteral("saved"),
                 true},
                {QStringLiteral(
                     "generated"),
                 false},
                {QStringLiteral(
                     "generation_skipped"),
                 !options.generate},
                {QStringLiteral(
                     "artifacts"),
                 artifacts},
            };
        response.exitCode =
            ExitCode::revisionConflict;
        return response;
    }
    if (!verification.open.manifest ||
        !verification.open.workspace ||
        hasErrors(
            response.diagnostics) ||
        verification.open.workspace->id !=
            project.workspace.id) {
        if (!hasErrors(
                response.diagnostics)) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    projectVerificationFailureCode,
                    "The newly saved project did not reopen with the expected Workspace identity.",
                    response.project,
                    project.workspace.id));
        }
        response.error =
            QStringLiteral(
                "Project was saved but could not be verified by reopening it; generated outputs were not written.");
        response.result =
            QJsonObject{
                {QStringLiteral(
                     "workspace_id"),
                 fromUtf8(
                     project.workspace.id)},
                {QStringLiteral(
                     "workspace_name"),
                 fromUtf8(
                     project.workspace.name)},
                {QStringLiteral("saved"),
                 true},
                {QStringLiteral(
                     "generated"),
                 false},
                {QStringLiteral(
                     "generation_skipped"),
                 !options.generate},
                {QStringLiteral(
                     "artifacts"),
                 artifacts},
            };
        response.exitCode =
            ExitCode::writeError;
        return response;
    }

    bool artifactWritesSuccessful = true;
    if (options.generate) {
        ArtifactWriteReport report =
            writeArtifactsWithReport(
                generation.artifacts);
        artifactWritesSuccessful =
            report.successful();
        artifacts =
            std::move(report.artifacts);
        appendUniqueDiagnostics(
            response.diagnostics,
            std::move(
                report.diagnostics));
    }
    const bool generated =
        options.generate &&
        artifactWritesSuccessful &&
        !hasErrors(
            response.diagnostics);
    response.ok =
        artifactWritesSuccessful &&
        !hasErrors(
            response.diagnostics);
    const QJsonObject projectSummary =
        summaryJson(
            project.workspace,
            project.manifest);
    QJsonObject result{
        {QStringLiteral(
             "workspace_id"),
         fromUtf8(
             project.workspace.id)},
        {QStringLiteral(
             "workspace_name"),
         fromUtf8(
             project.workspace
                 .name)},
        {QStringLiteral("saved"),
         true},
        {QStringLiteral(
             "generated"),
         generated},
        {QStringLiteral(
             "generation_skipped"),
         !options.generate},
        {QStringLiteral(
             "artifacts"),
         artifacts},
        {QStringLiteral("summary"),
         projectSummary},
    };
    if (options.generate &&
        !generated) {
        addGenerationRecovery(
            result,
            response.project,
            QStringLiteral(
                "generated_outputs_incomplete"),
            response.revision,
            options.targets);
    }
    response.result =
        result;
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::writeError;
    return response;
}

[[nodiscard]] CommandResponse runSummary(
    const QString& projectPath)
{
    OpenedProject opened =
        open(projectPath);
    CommandResponse response;
    response.command =
        QStringLiteral("summary");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.manifest ||
        !opened.open.workspace) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    response.result = summaryJson(
        *opened.open.workspace,
        *opened.open.manifest);
    response.ok =
        !hasErrors(
            response.diagnostics);
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::projectError;
    return response;
}

[[nodiscard]] CommandResponse runValidate(
    const QString& projectPath)
{
    OpenedProject opened =
        open(projectPath);
    CommandResponse response;
    response.command =
        QStringLiteral("validate");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    response.ok =
        opened.open.manifest &&
        opened.open.workspace &&
        !hasErrors(
            response.diagnostics);
    response.result =
        QJsonObject{
            {QStringLiteral(
                 "error_count"),
             static_cast<int>(
                 std::ranges::count_if(
                     response.diagnostics,
                     [](const Diagnostic&
                            diagnostic) {
                         return diagnostic
                                    .severity ==
                             DiagnosticSeverity::
                                 error;
                     }))},
            {QStringLiteral(
                 "warning_count"),
             static_cast<int>(
                 std::ranges::count_if(
                     response.diagnostics,
                     [](const Diagnostic&
                            diagnostic) {
                         return diagnostic
                                    .severity ==
                             DiagnosticSeverity::
                                 warning;
                     }))},
        };
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::projectError;
    return response;
}

[[nodiscard]] CommandResponse runDiff(
    const QStringList& arguments)
{
    DiffOptions options;
    QString optionError;
    QString optionSuggestion;
    if (!parseDiffOptions(
            arguments,
            options,
            optionError,
            optionSuggestion)) {
        return usageError(
            QStringLiteral("diff"),
            optionError,
            optionSuggestion);
    }
    OpenedProject before =
        open(
            options
                .beforeProjectPath);
    OpenedProject after =
        open(
            options
                .afterProjectPath);
    CommandResponse response;
    response.command =
        QStringLiteral("diff");
    response.diagnostics =
        std::move(
            before.open
                .diagnostics);
    appendUniqueDiagnostics(
        response.diagnostics,
        std::move(
            after.open
                .diagnostics));

    const auto revisionValue =
        [](const QString& revision) {
            return revision.isEmpty()
                ? QJsonValue(
                      QJsonValue::Null)
                : QJsonValue(revision);
        };
    const auto contextResult =
        [&]() {
            return QJsonObject{
                {QStringLiteral(
                     "before_project"),
                 before.path},
                {QStringLiteral(
                     "after_project"),
                 after.path},
                {QStringLiteral(
                     "before_revision"),
                 revisionValue(
                     before.revision)},
                {QStringLiteral(
                     "after_revision"),
                 revisionValue(
                     after.revision)},
                {QStringLiteral(
                     "kind_filter"),
                 options.kind},
                {QStringLiteral(
                     "require_equal"),
                 options.requireEqual},
                {QStringLiteral(
                     "writes_performed"),
                 false},
            };
        };

    if (before.open.workspace &&
        before.revision.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Before-project revision could not be calculated.",
                before.path));
    }
    if (after.open.workspace &&
        after.revision.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "After-project revision could not be calculated.",
                after.path));
    }
    if (!before.open.manifest ||
        !before.open.workspace ||
        !after.open.manifest ||
        !after.open.workspace ||
        hasErrors(
            response.diagnostics)) {
        response.error =
            QStringLiteral(
                "Both projects must open and validate before they can be compared.");
        response.result =
            contextResult();
        response.exitCode =
            ExitCode::projectError;
        return response;
    }

    const bool beforeRevisionMismatch =
        !options
             .expectedBeforeRevision
             .isEmpty() &&
        options
                .expectedBeforeRevision !=
            before.revision;
    const bool afterRevisionMismatch =
        !options
             .expectedAfterRevision
             .isEmpty() &&
        options
                .expectedAfterRevision !=
            after.revision;
    if (beforeRevisionMismatch ||
        afterRevisionMismatch) {
        if (beforeRevisionMismatch) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    revisionConflictCode,
                    QStringLiteral(
                        "Expected before revision '%1' but found '%2'.")
                        .arg(
                            options
                                .expectedBeforeRevision,
                            before.revision)
                        .toUtf8()
                        .toStdString(),
                    before.path));
        }
        if (afterRevisionMismatch) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    revisionConflictCode,
                    QStringLiteral(
                        "Expected after revision '%1' but found '%2'.")
                        .arg(
                            options
                                .expectedAfterRevision,
                            after.revision)
                        .toUtf8()
                        .toStdString(),
                    after.path));
        }
        QJsonObject result =
            contextResult();
        result.insert(
            QStringLiteral(
                "expected_before_revision"),
            revisionValue(
                options
                    .expectedBeforeRevision));
        result.insert(
            QStringLiteral(
                "expected_after_revision"),
            revisionValue(
                options
                    .expectedAfterRevision));
        response.error =
            QStringLiteral(
                "A project revision does not match the diff precondition; no comparison result was returned.");
        response.result =
            std::move(result);
        response.exitCode =
            ExitCode::revisionConflict;
        return response;
    }

    const std::vector<ModelChange>
        allChanges =
        diffWorkspaces(
            *before.open.workspace,
            *after.open.workspace);
    const QString beforeRevisionAfter =
        fileRevision(before.path);
    const QString afterRevisionAfter =
        fileRevision(after.path);
    if (beforeRevisionAfter.isEmpty() ||
        afterRevisionAfter.isEmpty()) {
        if (beforeRevisionAfter.isEmpty()) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    revisionReadFailureCode,
                    "Before-project revision could not be rechecked after comparison.",
                    before.path));
        }
        if (afterRevisionAfter.isEmpty()) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    revisionReadFailureCode,
                    "After-project revision could not be rechecked after comparison.",
                    after.path));
        }
        QJsonObject result =
            contextResult();
        result.insert(
            QStringLiteral(
                "before_current_revision"),
            revisionValue(
                beforeRevisionAfter));
        result.insert(
            QStringLiteral(
                "after_current_revision"),
            revisionValue(
                afterRevisionAfter));
        response.error =
            QStringLiteral(
                "A project revision could not be rechecked; no comparison result was returned.");
        response.result =
            std::move(result);
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    if (beforeRevisionAfter !=
            before.revision ||
        afterRevisionAfter !=
            after.revision) {
        if (beforeRevisionAfter !=
            before.revision) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    revisionConflictCode,
                    QStringLiteral(
                        "Before project changed from '%1' to '%2' during comparison.")
                        .arg(
                            before.revision,
                            beforeRevisionAfter)
                        .toUtf8()
                        .toStdString(),
                    before.path));
        }
        if (afterRevisionAfter !=
            after.revision) {
            response.diagnostics.push_back(
                cliDiagnostic(
                    revisionConflictCode,
                    QStringLiteral(
                        "After project changed from '%1' to '%2' during comparison.")
                        .arg(
                            after.revision,
                            afterRevisionAfter)
                        .toUtf8()
                        .toStdString(),
                    after.path));
        }
        QJsonObject result =
            contextResult();
        result.insert(
            QStringLiteral(
                "before_current_revision"),
            beforeRevisionAfter);
        result.insert(
            QStringLiteral(
                "after_current_revision"),
            afterRevisionAfter);
        response.error =
            QStringLiteral(
                "A project changed during comparison; retry diff.");
        response.result =
            std::move(result);
        response.exitCode =
            ExitCode::revisionConflict;
        return response;
    }

    std::vector<const ModelChange*>
        matchingChanges;
    matchingChanges.reserve(
        allChanges.size());
    for (const ModelChange& change :
         allChanges) {
        if (options.kind ==
                QStringLiteral("all") ||
            objectKindToken(
                change.objectKind) ==
                options.kind) {
            matchingChanges.push_back(
                &change);
        }
    }
    const qsizetype totalCount =
        static_cast<qsizetype>(
            matchingChanges.size());
    const qsizetype start =
        std::min(
            options.offset,
            totalCount);
    const qsizetype available =
        totalCount - start;
    const qsizetype returnedCount =
        options.limit
        ? std::min(
              *options.limit,
              available)
        : available;

    const DiffObjectStates beforeStates =
        collectDiffObjectStates(
            *before.open.workspace);
    const DiffObjectStates afterStates =
        collectDiffObjectStates(
            *after.open.workspace);
    const auto descriptorMap =
        [](const Workspace& workspace) {
            std::map<
                QString,
                ObjectDescriptor,
                std::less<>>
                result;
            for (ObjectDescriptor descriptor :
                 describeObjects(
                     workspace)) {
                result.insert_or_assign(
                    descriptor.id,
                    std::move(
                        descriptor));
            }
            return result;
        };
    const auto beforeDescriptors =
        descriptorMap(
            *before.open.workspace);
    const auto afterDescriptors =
        descriptorMap(
            *after.open.workspace);
    const auto stateFor =
        [](const DiffObjectStates& states,
           const ObjectId& id)
        -> const QJsonObject* {
            const auto found =
                states.find(id);
            return found == states.end()
                ? nullptr
                : &found->second;
        };
    const auto navigationFor =
        [](const auto& descriptors,
           const QString& project,
           const ObjectId& id)
        -> QJsonValue {
            const auto found =
                descriptors.find(
                    fromUtf8(id));
            return found ==
                    descriptors.end()
                ? QJsonValue(
                      QJsonValue::Null)
                : QJsonValue(
                      workbenchNavigation(
                          project,
                          found->second.id,
                          found->second.kind,
                          found->second.path));
        };
    QJsonArray changesJson;
    int addedCount = 0;
    int removedCount = 0;
    int modifiedCount = 0;
    for (const ModelChange*
             change :
         matchingChanges) {
        switch (change->change) {
        case ChangeKind::added:
            ++addedCount;
            break;
        case ChangeKind::removed:
            ++removedCount;
            break;
        case ChangeKind::modified:
            ++modifiedCount;
            break;
        }
    }
    for (qsizetype index = 0;
         index < returnedCount;
         ++index) {
        const ModelChange& change =
            *matchingChanges.at(
                static_cast<std::size_t>(
                    start +
                    index));
        QJsonObject changeJson =
            modelChangeJson(
                change,
                stateFor(
                    beforeStates,
                    change.id),
                stateFor(
                    afterStates,
                    change.id));
        changeJson.insert(
            QStringLiteral(
                "before_navigation"),
            navigationFor(
                beforeDescriptors,
                before.path,
                change.id));
        changeJson.insert(
            QStringLiteral(
                "after_navigation"),
            navigationFor(
                afterDescriptors,
                after.path,
                change.id));
        changesJson.append(
            std::move(changeJson));
    }
    QJsonObject result =
        contextResult();
    result.insert(
        QStringLiteral("changed"),
        !matchingChanges.empty());
    result.insert(
        QStringLiteral(
            "change_count"),
        static_cast<qint64>(
            totalCount));
    result.insert(
        QStringLiteral("counts"),
        QJsonObject{
            {QStringLiteral("added"),
             addedCount},
            {QStringLiteral("removed"),
             removedCount},
            {QStringLiteral("modified"),
             modifiedCount},
        });
    result.insert(
        QStringLiteral("changes"),
        changesJson);
    response.result =
        std::move(result);
    response.resultMetadata =
        paginationMetadata(
            totalCount,
            options.offset,
            returnedCount,
            options.limit);
    if (options.requireEqual &&
        !matchingChanges.empty()) {
        response.ok = false;
        response.error =
            QStringLiteral(
                "%1 change(s) found in the selected comparison scope.")
                .arg(totalCount);
        response.diagnostics.push_back(
            cliDiagnostic(
                differencesFoundCode,
                response.error
                    .toUtf8()
                    .toStdString(),
                after.path));
        response.exitCode =
            ExitCode::
                differencesFound;
    } else {
        response.ok = true;
        response.exitCode =
            ExitCode::success;
    }
    return response;
}

[[nodiscard]] CommandResponse runStatus(
    const QStringList& arguments)
{
    StatusOptions options;
    QString optionError;
    QString optionSuggestion;
    if (!parseStatusOptions(
            arguments,
            options,
            optionError,
            optionSuggestion)) {
        return usageError(
            QStringLiteral("status"),
            optionError,
            optionSuggestion);
    }
    OpenedProject opened =
        open(options.projectPath);
    CommandResponse response;
    response.command =
        QStringLiteral("status");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.manifest ||
        !opened.open.workspace ||
        hasErrors(
            response.diagnostics)) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    ProjectManifest generationManifest;
    if (!selectGenerationManifest(
            *opened.open.manifest,
            options.targets,
            opened.path,
            generationManifest,
            response.diagnostics)) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    GenerationResult generation =
        generateArtifacts(
            *opened.open.workspace,
            generationManifest);
    response.diagnostics.insert(
        response.diagnostics.end(),
        std::make_move_iterator(
            generation
                .diagnostics.begin()),
        std::make_move_iterator(
            generation
                .diagnostics.end()));
    if (hasErrors(
            response.diagnostics)) {
        response.error =
            QStringLiteral(
                "Artifact generation failed while checking status.");
        response.exitCode =
            ExitCode::generationError;
        return response;
    }

    QJsonArray artifacts;
    int synchronizedCount = 0;
    for (const auto& artifact :
         generation.artifacts) {
        QJsonObject status =
            artifactStatusJson(artifact);
        if (status.value(
                      QStringLiteral(
                          "synchronized"))
                .toBool()) {
            ++synchronizedCount;
        }
        artifacts.append(
            std::move(status));
    }
    const int outputCount =
        static_cast<int>(
            generation.artifacts.size());
    const QString revisionAfterCheck =
        fileRevision(opened.path);
    if (revisionAfterCheck.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Project revision could not be rechecked after inspecting outputs.",
                opened.path));
        response.result =
            QJsonObject{
                {QStringLiteral(
                     "outputs_current"),
                 QJsonValue(
                     QJsonValue::Null)},
                {QStringLiteral(
                     "checked_revision"),
                 opened.revision},
                {QStringLiteral(
                     "current_revision"),
                 QJsonValue(
                     QJsonValue::Null)},
                {QStringLiteral(
                     "artifacts"),
                 QJsonArray{}},
            };
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    if (revisionAfterCheck !=
        opened.revision) {
        response.revision =
            revisionAfterCheck;
        response.error =
            QStringLiteral(
                "Project revision changed while output status was being checked; retry status.");
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionConflictCode,
                QStringLiteral(
                    "Expected revision '%1' but found '%2'.")
                    .arg(
                        opened.revision,
                        revisionAfterCheck)
                    .toUtf8()
                    .toStdString(),
                opened.path));
        response.result =
            QJsonObject{
                {QStringLiteral(
                     "outputs_current"),
                 QJsonValue(
                     QJsonValue::Null)},
                {QStringLiteral(
                     "checked_revision"),
                 opened.revision},
                {QStringLiteral(
                     "current_revision"),
                 revisionAfterCheck},
                {QStringLiteral(
                     "artifacts"),
                 QJsonArray{}},
            };
        response.exitCode =
            ExitCode::revisionConflict;
        return response;
    }

    response.ok = true;
    QJsonObject statusResult{
            {QStringLiteral(
                 "outputs_current"),
             synchronizedCount ==
                 outputCount},
            {QStringLiteral(
                 "output_count"),
             outputCount},
            {QStringLiteral(
                 "synchronized_count"),
             synchronizedCount},
            {QStringLiteral(
                 "attention_count"),
             outputCount -
                 synchronizedCount},
            {QStringLiteral(
                 "checked_revision"),
             opened.revision},
            {QStringLiteral(
                 "current_revision"),
             revisionAfterCheck},
            {QStringLiteral(
                 "artifacts"),
             artifacts},
        };
    if (synchronizedCount !=
        outputCount) {
        QJsonArray recoveryArguments{
            QStringLiteral("generate"),
            opened.path,
            QStringLiteral("--expect"),
            opened.revision,
        };
        for (const QString& token :
             generationTargetArguments(
                 options.targets)) {
            recoveryArguments.append(token);
        }
        statusResult.insert(
            QStringLiteral(
                "recovery"),
            QJsonObject{
                {QStringLiteral(
                     "command"),
                 QStringLiteral(
                     "generate")},
                {QStringLiteral(
                     "project"),
                 opened.path},
                {QStringLiteral(
                     "expected_revision"),
                 opened.revision},
                {QStringLiteral(
                     "arguments"),
                 recoveryArguments},
            });
    }
    response.result =
        std::move(statusResult);
    if (options.requireCurrent &&
        synchronizedCount !=
            outputCount) {
        response.ok = false;
        response.error =
            QStringLiteral(
                "%1 configured output(s) require regeneration.")
                .arg(
                    outputCount -
                    synchronizedCount);
        response.diagnostics.push_back(
            cliDiagnostic(
                outputsOutOfDateCode,
                response.error
                    .toUtf8()
                    .toStdString(),
                opened.path));
        response.exitCode =
            ExitCode::
                outputsOutOfDate;
    }
    return response;
}

[[nodiscard]] CommandResponse runList(
    const QStringList& arguments)
{
    QString kind =
        QStringLiteral("all");
    QString parent;
    QString tag;
    QString expectedRevision;
    std::optional<qsizetype> limit{
        defaultQueryLimit};
    qsizetype offset = 0;
    bool sawKind = false;
    bool sawParent = false;
    bool recursive = false;
    bool sawRecursive = false;
    bool sawTag = false;
    bool sawLimit = false;
    bool sawAll = false;
    bool sawOffset = false;
    bool sawExpect = false;
    for (qsizetype index = 2;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral("--kind")) {
            if (sawKind) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--kind can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--kind requires a value."));
            }
            sawKind = true;
            kind =
                arguments.at(++index)
                    .toLower();
            continue;
        }
        if (token ==
            QStringLiteral("--parent")) {
            if (sawParent) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--parent can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--parent requires a value."));
            }
            sawParent = true;
            parent =
                arguments.at(++index);
            if (parent.isEmpty()) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--parent requires a non-empty stable ID."));
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--recursive")) {
            if (sawRecursive) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--recursive can be specified only once."));
            }
            sawRecursive = true;
            recursive = true;
            continue;
        }
        if (token ==
            QStringLiteral("--tag")) {
            if (sawTag) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--tag can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--tag requires a value."));
            }
            sawTag = true;
            tag =
                arguments.at(++index);
            if (tag.isEmpty()) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--tag requires a non-empty exact tag."));
            }
            continue;
        }
        if (token ==
            QStringLiteral("--offset")) {
            if (sawOffset) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--offset can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--offset requires a value."));
            }
            sawOffset = true;
            if (!parseBoundedSize(
                    arguments.at(++index),
                    true,
                    offset)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--offset must be a non-negative integer."));
            }
            continue;
        }
        if (token ==
            QStringLiteral("--limit")) {
            if (sawLimit) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--limit can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--limit requires a value."));
            }
            sawLimit = true;
            qsizetype parsedLimit = 0;
            if (!parseBoundedSize(
                    arguments.at(++index),
                    false,
                    parsedLimit)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--limit must be a positive integer."));
            }
            limit = parsedLimit;
            continue;
        }
        if (token ==
            QStringLiteral("--all")) {
            if (sawAll) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--all can be specified only once."));
            }
            sawAll = true;
            limit.reset();
            continue;
        }
        if (token ==
            QStringLiteral("--expect")) {
            if (sawExpect) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--expect can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--expect requires a revision."));
            }
            sawExpect = true;
            expectedRevision =
                arguments.at(++index);
            if (!validRevision(
                    expectedRevision)) {
                return usageError(
                    QStringLiteral("list"),
                    QStringLiteral(
                        "--expect must be a lowercase sha256:<64 hex digits> revision."));
            }
            continue;
        }
        return usageError(
            QStringLiteral("list"),
            unknownOptionMessage(
                QStringLiteral("list"),
                token),
            suggestedOption(
                QStringLiteral("list"),
                token));
    }
    if (recursive && !sawParent) {
        return usageError(
            QStringLiteral("list"),
            QStringLiteral(
                "--recursive requires --parent <stable-id>."));
    }
    if (sawAll && sawLimit) {
        return usageError(
            QStringLiteral("list"),
            QStringLiteral(
                "--all cannot be combined with --limit."));
    }
    if (!validKind(kind)) {
        return usageError(
            QStringLiteral("list"),
            QStringLiteral(
                "Unsupported object kind '%1'. Expected workspace, page, block, register, field, enum, or all.")
                .arg(kind));
    }

    OpenedProject opened =
        open(arguments.at(1));
    CommandResponse response;
    response.command =
        QStringLiteral("list");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.workspace) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    if (!enforceReadRevision(
            response,
            expectedRevision)) {
        return response;
    }
    const std::vector<ObjectDescriptor>
        objects = describeObjects(
            *opened.open.workspace);
    const DescriptorParents parents =
        descriptorParents(objects);
    if (!parent.isEmpty() &&
        !parents.contains(parent)) {
        response.diagnostics.push_back(
            objectNotFound(
                parent,
                response.project));
        response.result = QJsonArray{};
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    std::vector<ObjectDescriptor>
        matches;
    for (const auto& object :
         objects) {
        if (kind !=
                QStringLiteral("all") &&
            object.kind != kind) {
            continue;
        }
        if (!descriptorInParentScope(
                object,
                parent,
                recursive,
                parents)) {
            continue;
        }
        if (!descriptorHasTag(
                object, tag)) {
            continue;
        }
        matches.push_back(object);
    }
    const qsizetype totalCount =
        static_cast<qsizetype>(
            matches.size());
    const qsizetype start =
        std::min(offset, totalCount);
    const qsizetype available =
        totalCount - start;
    const qsizetype returnedCount =
        limit
        ? std::min(*limit, available)
        : available;
    QJsonArray result;
    for (qsizetype resultIndex = 0;
         resultIndex < returnedCount;
         ++resultIndex) {
        const auto& object =
            matches.at(
                static_cast<std::size_t>(
                    start +
                    resultIndex));
        QJsonObject item =
            descriptorJson(object);
        item.insert(
            QStringLiteral(
                "workbench_navigation"),
            workbenchNavigation(
                opened.path,
                object.id,
                object.kind,
                object.path));
        result.append(item);
    }
    response.result = result;
    response.resultMetadata =
        paginationMetadata(
            totalCount,
            offset,
            returnedCount,
            limit);
    response.ok =
        !hasErrors(
            response.diagnostics);
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::projectError;
    return response;
}

[[nodiscard]] CommandResponse runFind(
    const QStringList& arguments)
{
    if (arguments.size() < 3) {
        return usageError(
            QStringLiteral("find"),
            QStringLiteral(
                "find requires a project path and non-empty query."));
    }
    const QString query =
        arguments.at(2)
            .trimmed();
    if (query.isEmpty()) {
        return usageError(
            QStringLiteral("find"),
            QStringLiteral(
                "find query must not be empty."));
    }

    QString kind =
        QStringLiteral("all");
    QString parent;
    QString tag;
    QString expectedRevision;
    std::optional<qsizetype> limit{
        defaultQueryLimit};
    qsizetype offset = 0;
    bool sawKind = false;
    bool sawParent = false;
    bool recursive = false;
    bool sawRecursive = false;
    bool exact = false;
    bool sawExact = false;
    bool requireOne = false;
    bool sawRequireOne = false;
    bool sawTag = false;
    bool sawLimit = false;
    bool sawAll = false;
    bool sawOffset = false;
    bool sawExpect = false;
    for (qsizetype index = 3;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        if (token ==
            QStringLiteral("--kind")) {
            if (sawKind) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--kind can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--kind requires a value."));
            }
            sawKind = true;
            kind =
                arguments.at(++index)
                    .toLower();
            continue;
        }
        if (token ==
            QStringLiteral("--parent")) {
            if (sawParent) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--parent can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--parent requires a value."));
            }
            sawParent = true;
            parent =
                arguments.at(++index);
            if (parent.isEmpty()) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--parent requires a non-empty stable ID."));
            }
            continue;
        }
        if (token ==
            QStringLiteral(
                "--recursive")) {
            if (sawRecursive) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--recursive can be specified only once."));
            }
            sawRecursive = true;
            recursive = true;
            continue;
        }
        if (token ==
            QStringLiteral("--exact")) {
            if (sawExact) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--exact can be specified only once."));
            }
            sawExact = true;
            exact = true;
            continue;
        }
        if (token ==
            QStringLiteral(
                "--require-one")) {
            if (sawRequireOne) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--require-one can be specified only once."));
            }
            sawRequireOne = true;
            requireOne = true;
            continue;
        }
        if (token ==
            QStringLiteral("--tag")) {
            if (sawTag) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--tag can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--tag requires a value."));
            }
            sawTag = true;
            tag =
                arguments.at(++index);
            if (tag.isEmpty()) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--tag requires a non-empty exact tag."));
            }
            continue;
        }
        if (token ==
            QStringLiteral("--offset")) {
            if (sawOffset) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--offset can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--offset requires a value."));
            }
            sawOffset = true;
            if (!parseBoundedSize(
                    arguments.at(++index),
                    true,
                    offset)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--offset must be a non-negative integer."));
            }
            continue;
        }
        if (token ==
            QStringLiteral("--limit")) {
            if (sawLimit) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--limit can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--limit requires a value."));
            }
            sawLimit = true;
            bool validLimit = false;
            const qlonglong parsed =
                arguments.at(++index)
                    .toLongLong(
                        &validLimit);
            if (!validLimit ||
                parsed < 1 ||
                static_cast<
                    unsigned long long>(
                    parsed) >
                    static_cast<
                        unsigned long long>(
                        std::numeric_limits<
                            qsizetype>::
                            max())) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--limit must be a positive integer."));
            }
            limit =
                static_cast<qsizetype>(
                    parsed);
            continue;
        }
        if (token ==
            QStringLiteral("--all")) {
            if (sawAll) {
                return usageError(
                    QStringLiteral("find"),
                    QStringLiteral(
                        "--all can be specified only once."));
            }
            sawAll = true;
            limit.reset();
            continue;
        }
        if (token ==
            QStringLiteral("--expect")) {
            if (sawExpect) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--expect can be specified only once."));
            }
            if (optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--expect requires a revision."));
            }
            sawExpect = true;
            expectedRevision =
                arguments.at(++index);
            if (!validRevision(
                    expectedRevision)) {
                return usageError(
                    QStringLiteral(
                        "find"),
                    QStringLiteral(
                        "--expect must be a lowercase sha256:<64 hex digits> revision."));
            }
            continue;
        }
        return usageError(
            QStringLiteral("find"),
            unknownOptionMessage(
                QStringLiteral("find"),
                token),
            suggestedOption(
                QStringLiteral("find"),
                token));
    }
    if (requireOne &&
        (sawOffset || sawLimit || sawAll)) {
        return usageError(
            QStringLiteral("find"),
            QStringLiteral(
                "--require-one cannot be combined with --offset, --limit, or --all."));
    }
    if (sawAll && sawLimit) {
        return usageError(
            QStringLiteral("find"),
            QStringLiteral(
                "--all cannot be combined with --limit."));
    }
    if (recursive && !sawParent) {
        return usageError(
            QStringLiteral("find"),
            QStringLiteral(
                "--recursive requires --parent <stable-id>."));
    }
    if (!validKind(kind)) {
        return usageError(
            QStringLiteral("find"),
            QStringLiteral(
                "Unsupported object kind '%1'. Expected workspace, page, block, register, field, enum, or all.")
                .arg(kind));
    }
    if (requireOne) {
        limit.reset();
    }

    OpenedProject opened =
        open(arguments.at(1));
    CommandResponse response;
    response.command =
        QStringLiteral("find");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.workspace) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    if (!enforceReadRevision(
            response,
            expectedRevision)) {
        return response;
    }
    const std::vector<ObjectDescriptor>
        objects = describeObjects(
            *opened.open.workspace);
    const DescriptorParents parents =
        descriptorParents(objects);
    if (!parent.isEmpty() &&
        !parents.contains(parent)) {
        response.diagnostics.push_back(
            objectNotFound(
                parent,
                response.project));
        response.result = QJsonArray{};
        response.exitCode =
            ExitCode::projectError;
        return response;
    }

    std::vector<std::pair<
        SearchMatch,
        ObjectDescriptor>>
        matches;
    for (const auto& object :
         objects) {
        if (kind !=
                QStringLiteral("all") &&
            object.kind != kind) {
            continue;
        }
        if (!descriptorInParentScope(
                object,
                parent,
                recursive,
                parents)) {
            continue;
        }
        if (!descriptorHasTag(
                object, tag)) {
            continue;
        }
        const auto match =
            matchObject(
                object, query);
        if (match &&
            (!exact ||
             match->rank <= 1)) {
            matches.emplace_back(
                *match, object);
        }
    }
    std::ranges::stable_sort(
        matches,
        [](const auto& left,
           const auto& right) {
            return left.first.rank <
                right.first.rank;
        });
    QJsonArray result;
    const qsizetype totalCount =
        static_cast<qsizetype>(
            matches.size());
    const qsizetype start =
        std::min(offset, totalCount);
    const qsizetype available =
        totalCount - start;
    const qsizetype returnedCount =
        limit
        ? std::min(
              *limit,
              available)
        : available;
    for (qsizetype resultIndex = 0;
         resultIndex < returnedCount;
         ++resultIndex) {
        const auto& [matchInfo, object] =
            matches.at(
                static_cast<std::size_t>(
                    start +
                    resultIndex));
        QJsonObject matchJson =
            descriptorJson(object);
        matchJson.insert(
            QStringLiteral(
                "match_rank"),
            matchInfo.rank);
        matchJson.insert(
            QStringLiteral(
                "match_field"),
            matchInfo.field);
        matchJson.insert(
            QStringLiteral(
                "workbench_navigation"),
            workbenchNavigation(
                opened.path,
                object.id,
                object.kind,
                object.path));
        result.append(matchJson);
    }
    response.result = result;
    response.resultMetadata =
        paginationMetadata(
            totalCount,
            offset,
            returnedCount,
            limit);
    if (requireOne && totalCount == 0) {
        response.diagnostics.push_back(
            cliDiagnostic(
                objectNotFoundCode,
                QStringLiteral(
                    "find --require-one expected exactly one match for query '%1', but found none.")
                    .arg(query)
                    .toUtf8()
                    .toStdString(),
                response.project));
    } else if (
        requireOne && totalCount > 1) {
        response.diagnostics.push_back(
            cliDiagnostic(
                ambiguousFindMatchCode,
                QStringLiteral(
                    "find --require-one expected exactly one match for query '%1', but found %2.")
                    .arg(query)
                    .arg(totalCount)
                    .toUtf8()
                    .toStdString(),
                response.project));
    }
    response.ok =
        !hasErrors(
            response.diagnostics);
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::projectError;
    return response;
}

[[nodiscard]] CommandResponse runGet(
    const QStringList& arguments,
    const qsizetype optionTerminatorIndex)
{
    if (arguments.size() < 2) {
        return usageError(
            QStringLiteral("get"),
            QStringLiteral(
                "get requires a project path."));
    }
    QString id;
    QString expectedRevision;
    bool sawExpect = false;
    for (qsizetype index = 2;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        const bool optionRecognitionEnabled =
            optionTerminatorIndex < 0 ||
            index <
                optionTerminatorIndex;
        if (optionRecognitionEnabled &&
            token ==
                QStringLiteral(
                    "--expect")) {
            if (sawExpect) {
                return usageError(
                    QStringLiteral("get"),
                    QStringLiteral(
                        "--expect can be specified only once."));
            }
            if ((optionTerminatorIndex >=
                     0 &&
                 optionTerminatorIndex ==
                     index + 1) ||
                optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral("get"),
                    QStringLiteral(
                        "--expect requires a revision."));
            }
            sawExpect = true;
            expectedRevision =
                arguments.at(++index);
            if (!validRevision(
                    expectedRevision)) {
                return usageError(
                    QStringLiteral("get"),
                    QStringLiteral(
                        "--expect must be a lowercase sha256:<64 hex digits> revision."));
            }
            continue;
        }
        if (optionRecognitionEnabled &&
            token.startsWith(
                QLatin1Char('-'))) {
            return usageError(
                QStringLiteral("get"),
                unknownOptionMessage(
                    QStringLiteral("get"),
                    token),
                suggestedOption(
                    QStringLiteral("get"),
                    token));
        }
        if (!id.isEmpty()) {
            return usageError(
                QStringLiteral("get"),
                QStringLiteral(
                    "get accepts at most one stable object ID."));
        }
        id = token;
    }

    OpenedProject opened =
        open(arguments.at(1));
    CommandResponse response;
    response.command =
        QStringLiteral("get");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.workspace) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    if (!enforceReadRevision(
            response,
            expectedRevision)) {
        return response;
    }
    const QString targetId =
        id.isEmpty()
        ? fromUtf8(
              opened.open.workspace->id)
        : id;
    response.result =
        findObjectJson(
            *opened.open.workspace,
            targetId.toUtf8()
                .toStdString());
    if (response.result.isObject()) {
        QJsonObject object =
            response.result.toObject();
        const auto descriptor =
            describeObject(
                *opened.open.workspace,
                targetId);
        if (!descriptor) {
            response.diagnostics.push_back(
                objectNotFound(
                    targetId,
                    response.project));
            response.result =
                QJsonValue::Undefined;
            response.exitCode =
                ExitCode::projectError;
            return response;
        }
        object.insert(
            QStringLiteral(
                "workbench_navigation"),
            workbenchNavigation(
                opened.path,
                targetId,
                descriptor->kind,
                descriptor->path));
        response.result =
            std::move(object);
    }
    if (response.result.isUndefined()) {
        response.diagnostics.push_back(
            objectNotFound(
                targetId,
                response.project));
    }
    response.ok =
        !hasErrors(
            response.diagnostics);
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::projectError;
    return response;
}

[[nodiscard]] CommandResponse runGetMany(
    const QStringList& arguments,
    const qsizetype optionTerminatorIndex)
{
    if (arguments.size() < 3) {
        return usageError(
            QStringLiteral("get-many"),
            QStringLiteral(
                "get-many requires a project path and at least one stable object ID."));
    }

    QStringList requestedIds;
    QString expectedRevision;
    bool sawExpect = false;
    for (qsizetype index = 2;
         index < arguments.size();
         ++index) {
        const QString& token =
            arguments.at(index);
        const bool optionRecognitionEnabled =
            optionTerminatorIndex < 0 ||
            index <
                optionTerminatorIndex;
        if (optionRecognitionEnabled &&
            token ==
                QStringLiteral(
                    "--expect")) {
            if (sawExpect) {
                return usageError(
                    QStringLiteral(
                        "get-many"),
                    QStringLiteral(
                        "--expect can be specified only once."));
            }
            if ((optionTerminatorIndex >=
                     0 &&
                 optionTerminatorIndex ==
                     index + 1) ||
                optionValueMissing(
                    arguments,
                    index)) {
                return usageError(
                    QStringLiteral(
                        "get-many"),
                    QStringLiteral(
                        "--expect requires a revision."));
            }
            sawExpect = true;
            expectedRevision =
                arguments.at(++index);
            if (!validRevision(
                    expectedRevision)) {
                return usageError(
                    QStringLiteral(
                        "get-many"),
                    QStringLiteral(
                        "--expect must be a lowercase sha256:<64 hex digits> revision."));
            }
            continue;
        }
        if (optionRecognitionEnabled &&
            token.startsWith(
                QLatin1Char('-'))) {
            return usageError(
                QStringLiteral("get-many"),
                unknownOptionMessage(
                    QStringLiteral(
                        "get-many"),
                    token),
                suggestedOption(
                    QStringLiteral(
                        "get-many"),
                    token));
        }
        if (token.isEmpty()) {
            return usageError(
                QStringLiteral("get-many"),
                QStringLiteral(
                    "get-many stable object IDs must be non-empty."));
        }
        requestedIds.append(token);
    }
    if (requestedIds.isEmpty()) {
        return usageError(
            QStringLiteral("get-many"),
            QStringLiteral(
                "get-many requires at least one stable object ID."));
    }

    OpenedProject opened =
        open(arguments.at(1));
    CommandResponse response;
    response.command =
        QStringLiteral("get-many");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.workspace) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    if (!enforceReadRevision(
            response,
            expectedRevision)) {
        return response;
    }

    std::map<
        QString,
        ObjectDescriptor,
        std::less<>>
        descriptors;
    for (ObjectDescriptor descriptor :
         describeObjects(
             *opened.open.workspace)) {
        const QString descriptorId =
            descriptor.id;
        descriptors.insert_or_assign(
            descriptorId,
            std::move(descriptor));
    }
    std::map<
        QString,
        QJsonObject,
        std::less<>>
        foundObjects;
    std::set<
        QString,
        std::less<>>
        missingObjects;
    std::map<
        QString,
        qsizetype,
        std::less<>>
        firstIndexes;
    std::vector<Diagnostic>
        missingDiagnostics;
    QJsonArray items;
    int foundCount = 0;
    int missingCount = 0;
    int duplicateCount = 0;
    for (qsizetype index = 0;
         index < requestedIds.size();
         ++index) {
        const QString& requestedId =
            requestedIds.at(index);
        const auto [first, inserted] =
            firstIndexes.emplace(
                requestedId,
                index);
        if (!inserted) {
            ++duplicateCount;
        }

        if (!foundObjects.contains(
                requestedId) &&
            !missingObjects.contains(
                requestedId)) {
            const auto descriptor =
                descriptors.find(
                    requestedId);
            QJsonValue object =
                findObjectJson(
                    *opened.open.workspace,
                    requestedId.toUtf8()
                        .toStdString());
            if (descriptor ==
                    descriptors.end() ||
                !object.isObject()) {
                missingObjects.insert(
                    requestedId);
                missingDiagnostics.push_back(
                    objectNotFound(
                        requestedId,
                        response.project));
            } else {
                QJsonObject objectJson =
                    object.toObject();
                objectJson.insert(
                    QStringLiteral(
                        "workbench_navigation"),
                    workbenchNavigation(
                        opened.path,
                        requestedId,
                        descriptor->second.kind,
                        descriptor->second.path));
                foundObjects.insert_or_assign(
                    requestedId,
                    std::move(objectJson));
            }
        }

        const auto found =
            foundObjects.find(
                requestedId);
        const bool objectFound =
            found != foundObjects.end();
        if (objectFound) {
            ++foundCount;
        } else {
            ++missingCount;
        }
        items.append(
            QJsonObject{
                {QStringLiteral(
                     "request_index"),
                 static_cast<qint64>(
                     index)},
                {QStringLiteral(
                     "requested_id"),
                 requestedId},
                {QStringLiteral("found"),
                 objectFound},
                {QStringLiteral("object"),
                 objectFound
                     ? QJsonValue(
                           found->second)
                     : QJsonValue(
                           QJsonValue::Null)},
                {QStringLiteral(
                     "error_code"),
                 objectFound
                     ? QJsonValue(
                           QJsonValue::Null)
                     : QJsonValue(
                           fromUtf8(
                               objectNotFoundCode))},
                {QStringLiteral(
                     "duplicate_of_index"),
                 inserted
                     ? QJsonValue(
                           QJsonValue::Null)
                     : QJsonValue(
                           static_cast<qint64>(
                               first->second))},
            });
    }
    if (!missingDiagnostics.empty()) {
        response.diagnostics.insert(
            response.diagnostics.begin(),
            std::make_move_iterator(
                missingDiagnostics.begin()),
            std::make_move_iterator(
                missingDiagnostics.end()));
    }
    response.result =
        QJsonObject{
            {QStringLiteral(
                 "requested_count"),
             static_cast<qint64>(
                 requestedIds.size())},
            {QStringLiteral(
                 "found_count"),
             foundCount},
            {QStringLiteral(
                 "missing_count"),
             missingCount},
            {QStringLiteral(
                 "duplicate_count"),
             duplicateCount},
            {QStringLiteral("items"),
             items},
        };
    response.ok =
        missingCount == 0 &&
        !hasErrors(
            response.diagnostics);
    response.exitCode =
        response.ok
        ? ExitCode::success
        : ExitCode::projectError;
    return response;
}

[[nodiscard]] CommandResponse runGenerate(
    const QStringList& arguments)
{
    GenerateOptions options;
    QString optionErrorText;
    QString optionSuggestion;
    if (!parseGenerateOptions(
            arguments,
            options,
            optionErrorText,
            optionSuggestion)) {
        return usageError(
            QStringLiteral("generate"),
            optionErrorText,
            optionSuggestion);
    }

    OpenedProject opened =
        open(options.projectPath);
    CommandResponse response;
    response.command =
        QStringLiteral("generate");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.manifest ||
        !opened.open.workspace ||
        hasErrors(
            response.diagnostics)) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    ProjectManifest generationManifest;
    if (!selectGenerationManifest(
            *opened.open.manifest,
            options.targets,
            opened.path,
            generationManifest,
            response.diagnostics)) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }

    const auto resultJson =
        [&](bool written,
            QJsonValue outputsCurrent,
            const QString&
                currentRevision,
            const QJsonArray&
                artifacts) {
            return QJsonObject{
                {QStringLiteral(
                     "dry_run"),
                 options.dryRun},
                {QStringLiteral(
                     "written"),
                 written},
                {QStringLiteral(
                     "outputs_current"),
                 outputsCurrent},
                {QStringLiteral(
                     "generated_from_revision"),
                 opened.revision},
                {QStringLiteral(
                     "current_revision"),
                 currentRevision.isEmpty()
                     ? QJsonValue(
                           QJsonValue::
                               Null)
                     : QJsonValue(
                           currentRevision)},
                {QStringLiteral(
                     "artifacts"),
                 artifacts},
                {QStringLiteral(
                     "target_count"),
                 artifacts.size()},
            };
        };
    const auto revisionConflict =
        [&](const QString& expected,
            const QString& actual,
            const QJsonArray&
                artifacts,
            bool written,
            const QString& message) {
            CommandResponse conflict;
            conflict.command =
                QStringLiteral(
                    "generate");
            conflict.project =
                opened.path;
            conflict.revision =
                actual;
            conflict.error = message;
            conflict.diagnostics =
                response.diagnostics;
            conflict.diagnostics
                .push_back(
                    cliDiagnostic(
                        revisionConflictCode,
                        QStringLiteral(
                            "Expected revision '%1' but found '%2'.")
                            .arg(
                                expected,
                                actual)
                            .toUtf8()
                            .toStdString(),
                        opened.path));
            QJsonObject result =
                resultJson(
                    written,
                    false,
                    actual,
                    artifacts);
            if (written &&
                !actual.isEmpty()) {
                addGenerationRecovery(
                    result,
                    opened.path,
                    QStringLiteral(
                        "project_changed_after_output_write"),
                    actual,
                    options.targets);
            }
            conflict.result =
                result;
            conflict.exitCode =
                ExitCode::
                    revisionConflict;
            return conflict;
        };
    if (!options.expectedRevision
             .isEmpty() &&
        options.expectedRevision !=
            opened.revision) {
        return revisionConflict(
            options.expectedRevision,
            opened.revision,
            {},
            false,
            QStringLiteral(
                "Project revision does not match --expect; no outputs were written."));
    }

    GenerationResult generation =
        generateArtifacts(
            *opened.open.workspace,
            generationManifest);
    response.diagnostics.insert(
        response.diagnostics.end(),
        std::make_move_iterator(
            generation
                .diagnostics.begin()),
        std::make_move_iterator(
            generation
                .diagnostics.end()));
    QJsonArray artifacts =
        artifactPreviewJson(
            generation.artifacts);
    if (hasErrors(
            response.diagnostics)) {
        response.result =
            resultJson(
                false,
                false,
                opened.revision,
                artifacts);
        response.error =
            QStringLiteral(
                "Artifact generation failed; no files were written.");
        response.exitCode =
            ExitCode::generationError;
        return response;
    }

    const QString revisionBeforeWrite =
        fileRevision(opened.path);
    if (revisionBeforeWrite.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Project revision could not be rechecked; no outputs were written.",
                opened.path));
        response.result =
            resultJson(
                false,
                false,
                {},
                artifacts);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    if (revisionBeforeWrite !=
        opened.revision) {
        return revisionConflict(
            opened.revision,
            revisionBeforeWrite,
            artifacts,
            false,
            QStringLiteral(
                "Project revision changed during generation; no outputs were written."));
    }

    if (options.dryRun) {
        response.ok = true;
        response.result =
            resultJson(
                false,
                QJsonValue(
                    QJsonValue::Null),
                revisionBeforeWrite,
                artifacts);
        return response;
    }

    ArtifactWriteReport writeReport =
        writeArtifactsWithReport(
            generation.artifacts);
    artifacts =
        std::move(
            writeReport.artifacts);
    const bool outputsWritten =
        writeReport.successful();
    const bool anyOutputWritten =
        writeReport.writtenCount > 0;
    response.diagnostics.insert(
        response.diagnostics.end(),
        std::make_move_iterator(
            writeReport.diagnostics.begin()),
        std::make_move_iterator(
            writeReport.diagnostics.end()));
    const QString revisionAfterWrite =
        fileRevision(opened.path);
    if (revisionAfterWrite.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Outputs were processed, but the project revision could not be verified afterward.",
                opened.path));
        response.result =
            resultJson(
                anyOutputWritten,
                false,
                {},
                artifacts);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    response.revision =
        revisionAfterWrite;
    if (!outputsWritten ||
        hasErrors(
            response.diagnostics)) {
        QJsonObject result =
            resultJson(
                anyOutputWritten,
                false,
                revisionAfterWrite,
                artifacts);
        addGenerationRecovery(
            result,
            opened.path,
            QStringLiteral(
                "generated_outputs_incomplete"),
            revisionAfterWrite,
            options.targets);
        response.result =
            result;
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    if (revisionAfterWrite !=
        opened.revision) {
        return revisionConflict(
            opened.revision,
            revisionAfterWrite,
            artifacts,
            anyOutputWritten,
            QStringLiteral(
                "Project revision changed while outputs were being written; regenerate the current revision."));
    }

    response.ok = true;
    response.result =
        resultJson(
            anyOutputWritten,
            true,
            revisionAfterWrite,
            artifacts);
    return response;
}

[[nodiscard]] CommandResponse runApply(
    const QStringList& arguments,
    QTextStream& standardInput)
{
    ApplyOptions options;
    QString optionErrorText;
    QString optionSuggestion;
    if (!parseApplyOptions(
            arguments,
            options,
            optionErrorText,
            optionSuggestion)) {
        return usageError(
            QStringLiteral("apply"),
            optionErrorText,
            optionSuggestion);
    }

    QString patchErrorText;
    const auto patch =
        readPatch(
            options.patchPath,
            standardInput,
            patchErrorText);
    if (!patch) {
        CommandResponse response;
        response.command =
            QStringLiteral("apply");
        response.project =
            QFileInfo(options.projectPath)
                .absoluteFilePath();
        response.error =
            patchErrorText;
        response.result =
            QJsonObject{
                {QStringLiteral("stage"),
                 QStringLiteral("input")},
                {QStringLiteral("patch"),
                 options.patchPath},
                {QStringLiteral(
                     "writes_performed"),
                 false},
            };
        response.exitCode =
            ExitCode::inputError;
        return response;
    }
    const QString patchRevision =
        patch->value(
                  QStringLiteral(
                      "expected_revision"))
            .toString();
    if (!options.expectedRevision
             .isEmpty() &&
        !patchRevision.isEmpty() &&
        options.expectedRevision !=
            patchRevision) {
        return usageError(
            QStringLiteral("apply"),
            QStringLiteral(
                "--expect and patch expected_revision do not match."));
    }
    if (options.expectedRevision
            .isEmpty()) {
        options.expectedRevision =
            patchRevision;
    }
    if (options.force &&
        !options.expectedRevision
             .isEmpty()) {
        return usageError(
            QStringLiteral("apply"),
            QStringLiteral(
                "--force cannot be combined with patch expected_revision."));
    }
    if (!options.dryRun &&
        !options.force &&
        options.expectedRevision
            .isEmpty()) {
        return usageError(
            QStringLiteral("apply"),
            QStringLiteral(
                "A real apply requires expected_revision in the patch or --expect <revision>. Use summary to obtain the current revision, or pass --force explicitly."));
    }

    OpenedProject opened =
        open(options.projectPath);
    CommandResponse response;
    response.command =
        QStringLiteral("apply");
    response.project =
        opened.path;
    response.revision =
        opened.revision;
    response.diagnostics =
        std::move(
            opened.open
                .diagnostics);
    if (!opened.open.manifest ||
        !opened.open.workspace) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    const std::vector<Diagnostic>
        baselineValidation =
            validateWorkspace(
                *opened.open.workspace);
    response.diagnostics =
        withoutMatchingDiagnostics(
            std::move(
                response.diagnostics),
            baselineValidation);
    if (hasErrors(
            response.diagnostics)) {
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    const bool repairMode =
        hasErrors(
            baselineValidation);
    QJsonObject repair;

    const auto revisionConflict =
        [&](const QString& expected,
            const QString& actual,
            const QJsonArray& changes,
            bool changed) {
            CommandResponse conflict;
            conflict.command =
                QStringLiteral("apply");
            conflict.project =
                opened.path;
            conflict.revision =
                actual;
            conflict.error =
                QStringLiteral(
                    "Project revision changed; no changes were saved.");
            conflict.diagnostics.push_back(
                cliDiagnostic(
                    revisionConflictCode,
                    QStringLiteral(
                        "Expected revision '%1' but found '%2'.")
                        .arg(
                            expected,
                            actual)
                        .toUtf8()
                        .toStdString(),
                    opened.path));
            conflict.result =
                applyResult(
                    options.dryRun,
                    changed,
                    false,
                    false,
                    false,
                    opened.revision,
                    actual,
                    changes,
                    {},
                    repair);
            conflict.exitCode =
                ExitCode::
                    revisionConflict;
            return conflict;
        };
    if (!options.expectedRevision
             .isEmpty() &&
        options.expectedRevision !=
            opened.revision) {
        return revisionConflict(
            options.expectedRevision,
            opened.revision,
            {},
            false);
    }

    const QJsonArray operations =
        patch->value(
                  QStringLiteral(
                      "operations"))
            .toArray();
    Workspace candidate =
        *opened.open.workspace;
    QJsonArray changes;
    qsizetype failedIndex = -1;
    QString operationError;
    if (!applyOperations(
            candidate,
            operations,
            changes,
            failedIndex,
            operationError)) {
        response.error =
            QStringLiteral(
                "Patch operation %1 failed: %2")
                .arg(failedIndex)
                .arg(operationError);
        QJsonObject result =
            applyResult(
                options.dryRun,
                hasChanged(changes),
                false,
                false,
                false,
                opened.revision,
                opened.revision,
                changes,
                {});
        result.insert(
            QStringLiteral("failure"),
            operationFailureJson(
                failedIndex,
                operations,
                changes,
                operationError));
        response.result =
            result;
        response.exitCode =
            ExitCode::usageError;
        return response;
    }

    const bool changed =
        workspaceJson(candidate) !=
        workspaceJson(
            *opened.open.workspace);
    std::vector<Diagnostic>
        candidateValidation =
            validateWorkspace(
                candidate);
    if (repairMode) {
        repair =
            repairJson(
                baselineValidation,
                candidateValidation);
    }
    appendUniqueDiagnostics(
        response.diagnostics,
        candidateValidation);
    if (repairMode &&
        repair.value(
                  QStringLiteral(
                      "introduced_problem_count"))
                .toInteger() >
            0) {
        response.error =
            QStringLiteral(
                "Repair patch introduces a different validation problem; no files were written.");
        QJsonObject result =
            applyResult(
                options.dryRun,
                changed,
                false,
                false,
                false,
                opened.revision,
                opened.revision,
                changes,
                {},
                repair);
        result.insert(
            QStringLiteral("failure"),
            prewriteFailureJson(
                QStringLiteral(
                    "validation"),
                operations,
                changes,
                response.diagnostics,
                response.error));
        response.result =
            result;
        response.exitCode =
            ExitCode::projectError;
        return response;
    }
    if (hasErrors(
            response.diagnostics)) {
        if (repairMode) {
            response.error =
                QStringLiteral(
                    "Repair patch leaves validation errors; no files were written.");
        } else {
            response.error =
                QStringLiteral(
                    "Candidate validation failed; no files were written.");
        }
        QJsonObject result =
            applyResult(
                options.dryRun,
                changed,
                false,
                false,
                false,
                opened.revision,
                opened.revision,
                changes,
                {},
                repair);
        result.insert(
            QStringLiteral("failure"),
            prewriteFailureJson(
                QStringLiteral(
                    "validation"),
                operations,
                changes,
                response.diagnostics,
                response.error));
        response.result =
            result;
        response.exitCode =
            ExitCode::projectError;
        return response;
    }

    GenerationResult generation =
        generateArtifacts(
            candidate,
            *opened.open.manifest);
    appendUniqueDiagnostics(
        response.diagnostics,
        std::move(
            generation.diagnostics));
    QJsonArray artifacts =
        artifactPreviewJson(
            generation.artifacts);
    if (hasErrors(
            response.diagnostics)) {
        response.error =
            QStringLiteral(
                "Candidate output generation failed; no files were written.");
        QJsonObject result =
            applyResult(
                options.dryRun,
                changed,
                false,
                false,
                true,
                opened.revision,
                opened.revision,
                changes,
                artifacts,
                repair);
        result.insert(
            QStringLiteral("failure"),
            prewriteFailureJson(
                QStringLiteral(
                    "generation"),
                operations,
                changes,
                response.diagnostics,
                response.error));
        response.result =
            result;
        response.exitCode =
            ExitCode::generationError;
        return response;
    }

    if (options.dryRun) {
        response.ok = true;
        response.result =
            applyResult(
                true,
                changed,
                false,
                false,
                true,
                opened.revision,
                opened.revision,
                changes,
                artifacts,
                repair);
        return response;
    }

    const QString revisionBeforeSave =
        fileRevision(opened.path);
    if (revisionBeforeSave !=
        opened.revision) {
        return revisionConflict(
            options.force
                ? opened.revision
                : options
                      .expectedRevision,
            revisionBeforeSave,
            changes,
            changed);
    }

    if (!changed) {
        response.ok = true;
        response.result =
            applyResult(
                false,
                false,
                false,
                false,
                true,
                opened.revision,
                opened.revision,
                changes,
                artifacts,
                repair);
        return response;
    }

    appendUniqueDiagnostics(
        response.diagnostics,
        saveProjectFile(
            *opened.open.manifest,
            candidate));
    if (hasErrors(
            response.diagnostics)) {
        response.result =
            applyResult(
                false,
                true,
                false,
                false,
                true,
                opened.revision,
                opened.revision,
                changes,
                artifacts,
                repair);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }

    const QString newRevision =
        fileRevision(opened.path);
    if (newRevision.isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Project was saved, but its new revision could not be calculated.",
                opened.path));
        QJsonObject result =
            applyResult(
                false,
                true,
                true,
                false,
                true,
                opened.revision,
                {},
                changes,
                artifacts,
                repair);
        addGenerationRecovery(
            result,
            opened.path,
            QStringLiteral(
                "saved_revision_unavailable"));
        response.result = result;
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    response.revision =
        newRevision;

    const auto pendingResult =
        [&](const QString& reason,
            const QString&
                currentRevision,
            bool outputsWritten) {
            QJsonObject result =
                applyResult(
                    false,
                    true,
                    true,
                    false,
                    true,
                    opened.revision,
                    newRevision,
                    changes,
                    artifacts,
                    repair);
            result.insert(
                QStringLiteral(
                    "outputs_written"),
                outputsWritten);
            result.insert(
                QStringLiteral(
                    "outputs_current"),
                false);
            result.insert(
                QStringLiteral(
                    "current_revision"),
                currentRevision.isEmpty()
                    ? QJsonValue(
                          QJsonValue::Null)
                    : QJsonValue(
                          currentRevision));
            addGenerationRecovery(
                result,
                opened.path,
                reason,
                newRevision);
            return result;
        };
    const QString
        revisionBeforeOutputs =
            fileRevision(opened.path);
    if (revisionBeforeOutputs
            .isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Project was saved, but its revision could not be verified before writing outputs.",
                opened.path));
        response.result =
            pendingResult(
                QStringLiteral(
                    "project_revision_unavailable_before_output_write"),
                {},
                false);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    if (revisionBeforeOutputs !=
        newRevision) {
        response.revision =
            revisionBeforeOutputs;
        response.error =
            QStringLiteral(
                "Project revision changed after this apply saved; stale outputs were not written.");
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionConflictCode,
                QStringLiteral(
                    "Expected saved revision '%1' but found '%2' before output generation.")
                    .arg(
                        newRevision,
                        revisionBeforeOutputs)
                    .toUtf8()
                    .toStdString(),
                opened.path));
        response.result =
            pendingResult(
                QStringLiteral(
                    "project_changed_before_output_write"),
                revisionBeforeOutputs,
                false);
        response.exitCode =
            ExitCode::
                revisionConflict;
        return response;
    }

    ArtifactWriteReport writeReport =
        writeArtifactsWithReport(
            generation.artifacts);
    artifacts =
        std::move(
            writeReport.artifacts);
    const bool outputsWritten =
        writeReport.successful();
    appendUniqueDiagnostics(
        response.diagnostics,
        std::move(
            writeReport.diagnostics));
    const QString revisionAfterOutputs =
        fileRevision(opened.path);
    if (revisionAfterOutputs
            .isEmpty()) {
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionReadFailureCode,
                "Outputs were processed, but the project revision could not be verified afterward.",
                opened.path));
        response.result =
            pendingResult(
                QStringLiteral(
                    "project_revision_unavailable_after_output_write"),
                {},
                outputsWritten);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    response.revision =
        revisionAfterOutputs;
    if (!outputsWritten ||
        hasErrors(
            response.diagnostics)) {
        response.result =
            pendingResult(
                QStringLiteral(
                    "saved_model_has_pending_outputs"),
                revisionAfterOutputs,
                false);
        response.exitCode =
            ExitCode::writeError;
        return response;
    }
    if (revisionAfterOutputs !=
        newRevision) {
        response.error =
            QStringLiteral(
                "Project revision changed while outputs were being written; the outputs may be stale.");
        response.diagnostics.push_back(
            cliDiagnostic(
                revisionConflictCode,
                QStringLiteral(
                    "Outputs were generated from revision '%1', but the project is now '%2'.")
                    .arg(
                        newRevision,
                        revisionAfterOutputs)
                    .toUtf8()
                    .toStdString(),
                opened.path));
        response.result =
            pendingResult(
                QStringLiteral(
                    "project_changed_during_output_write"),
                revisionAfterOutputs,
                true);
        response.exitCode =
            ExitCode::
                revisionConflict;
        return response;
    }

    response.ok = true;
    QJsonObject result =
        applyResult(
            false,
            true,
            true,
            true,
            true,
            opened.revision,
            newRevision,
            changes,
            artifacts,
            repair);
    result.insert(
        QStringLiteral(
            "current_revision"),
        newRevision);
    response.result = result;
    return response;
}

} // namespace

int run(
    const QStringList& rawArguments,
    QTextStream& standardInput,
    QTextStream& standardOutput,
    QTextStream& standardError)
{
    QStringList arguments;
    arguments.reserve(
        rawArguments.size());
    bool json = false;
    bool helpRequested = false;
    bool optionsTerminated = false;
    qsizetype optionTerminatorIndex = -1;
    for (const QString& token :
         rawArguments) {
        if (!optionsTerminated &&
            token ==
                QStringLiteral("--")) {
            optionsTerminated = true;
            optionTerminatorIndex =
                arguments.size();
            continue;
        }
        if (!optionsTerminated &&
            token ==
                QStringLiteral(
                    "--json")) {
            json = true;
            continue;
        }
        if (!optionsTerminated &&
            (token ==
                 QStringLiteral(
                     "--help") ||
             token ==
                 QStringLiteral("-h"))) {
            helpRequested = true;
            continue;
        }
        arguments.push_back(
            token);
    }

    if (helpRequested) {
        const CommandResponse response =
            runHelp(
                arguments.isEmpty()
                    ? QString{}
                    : arguments.front());
        if (json) {
            writeJsonResponse(
                response,
                standardOutput);
        } else {
            writeTextResponse(
                response,
                standardOutput,
                standardError);
        }
        return static_cast<int>(
            response.exitCode);
    }
    if (arguments.isEmpty()) {
        const CommandResponse response =
            usageError(
                QString{},
                QStringLiteral(
                    "No command was provided."));
        if (json) {
            writeJsonResponse(
                response,
                standardOutput);
        } else {
            writeTextResponse(
                response,
                standardOutput,
                standardError);
        }
        return static_cast<int>(
            response.exitCode);
    }
    if (arguments.size() == 1 &&
        (arguments.front() ==
             QStringLiteral(
                 "--version") ||
         arguments.front() ==
             QStringLiteral(
                 "version"))) {
        if (json) {
            const CommandResponse response =
                runVersion();
            writeJsonResponse(
                response,
                standardOutput);
            return static_cast<int>(
                response.exitCode);
        }
        standardOutput
            << "regmapc "
            << fromUtf8(cliVersion)
            << '\n';
        standardOutput.flush();
        return static_cast<int>(
            ExitCode::success);
    }

    const QString command =
        arguments.front().toLower();
    CommandResponse response;
    if (command ==
        QStringLiteral("help")) {
        response =
            arguments.size() <= 2
            ? runHelp(
                  arguments.size() == 2
                  ? arguments.at(1)
                  : QString{})
            : usageError(
                  command,
                  QStringLiteral(
                      "help accepts at most one command name."));
    } else if (
        command ==
            QStringLiteral("version") ||
        command ==
            QStringLiteral("--version")) {
        response =
            usageError(
                QStringLiteral("version"),
                QStringLiteral(
                    "version accepts no positional arguments."));
    } else if (command ==
        QStringLiteral("schema")) {
        response =
            arguments.size() == 1
            ? runSchema()
            : usageError(
                  command,
                  QStringLiteral(
                      "schema accepts no positional arguments."));
    } else if (
        command ==
        QStringLiteral("init")) {
        response =
            runInit(arguments);
    } else if (
        command ==
            QStringLiteral("summary") ||
        command ==
            QStringLiteral("validate")) {
        if (arguments.size() != 2) {
            response = usageError(
                command,
                QStringLiteral(
                    "%1 requires exactly one project path.")
                    .arg(command));
        } else if (
            command ==
            QStringLiteral("summary")) {
            response =
                runSummary(
                    arguments.at(1));
        } else {
            response =
                runValidate(
                    arguments.at(1));
        }
    } else if (
        command ==
            QStringLiteral("diff")) {
        response =
            runDiff(arguments);
    } else if (
        command ==
            QStringLiteral("status")) {
        response =
            runStatus(arguments);
    } else if (
        command ==
        QStringLiteral("list")) {
        response =
            arguments.size() >= 2
            ? runList(arguments)
            : usageError(
                  command,
                  QStringLiteral(
                      "list requires a project path."));
    } else if (
        command ==
        QStringLiteral("find")) {
        response =
            runFind(arguments);
    } else if (
        command ==
            QStringLiteral("get")) {
        response =
            runGet(
                arguments,
                optionTerminatorIndex);
    } else if (
        command ==
            QStringLiteral("get-many")) {
        response =
            runGetMany(
                arguments,
                optionTerminatorIndex);
    } else if (
        command ==
        QStringLiteral("generate")) {
        response =
            arguments.size() >= 2
            ? runGenerate(arguments)
            : usageError(
                  command,
                  QStringLiteral(
                      "generate requires a project path."));
    } else if (
        command ==
        QStringLiteral("apply")) {
        response =
            runApply(
                arguments,
                standardInput);
    } else {
        const QString suggestion =
            suggestedCommand(
                command);
        response = usageError(
            command,
            suggestion.isEmpty()
                ? QStringLiteral(
                      "Unknown command '%1'.")
                      .arg(command)
                : QStringLiteral(
                      "Unknown command '%1'. Did you mean '%2'?")
                      .arg(
                          command,
                          suggestion));
        if (!suggestion.isEmpty()) {
            response.result =
                QJsonObject{
                    {QStringLiteral(
                         "suggested_command"),
                     suggestion},
                };
        }
    }

    if (json) {
        writeJsonResponse(
            response,
            standardOutput);
    } else {
        writeTextResponse(
            response,
            standardOutput,
            standardError);
    }
    return static_cast<int>(
        response.exitCode);
}

int run(
    const QStringList& arguments,
    QTextStream& standardOutput,
    QTextStream& standardError)
{
    QString emptyInput;
    QTextStream standardInput(
        &emptyInput,
        QIODevice::ReadOnly);
    return run(
        arguments,
        standardInput,
        standardOutput,
        standardError);
}

} // namespace regmap::cli
