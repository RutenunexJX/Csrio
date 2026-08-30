#include "suite_integration.hpp"

#include "main_window.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/validation.hpp"
#include "regmap/core/workspace_store.hpp"

#include <suiteapp/protocol.h>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

#include <filesystem>
#include <algorithm>
#include <optional>

namespace regmap::workbench {

namespace {

constexpr auto kOpenAction = "regmap.project.open";
constexpr auto kWorkbenchSurface = "regmap.workbench";

struct ProjectTarget {
    QString uri;
    QString projectPath;
    QString objectId;
    QString registerId;
    QString fieldId;

    [[nodiscard]] bool isValid() const
    {
        return !projectPath.trimmed().isEmpty();
    }
};

struct ObjectSummary {
    QString id;
    QString name;
    QString kind;
    QString parentId;

    [[nodiscard]] bool isValid() const
    {
        return !id.isEmpty();
    }
};

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

ProjectTarget projectTarget(const QJsonObject& params)
{
    ProjectTarget target;
    target.uri = params.value(QStringLiteral("resourceUri")).toString();
    if (target.uri.isEmpty())
        target.uri = params.value(QStringLiteral("uri")).toString();

    if (!target.uri.isEmpty()) {
        const QUrl uri(target.uri, QUrl::StrictMode);
        if (uri.isValid() &&
            uri.scheme().compare(QStringLiteral("regmap"),
                                 Qt::CaseInsensitive) == 0) {
            const QUrlQuery query(uri);
            target.projectPath = query.queryItemValue(
                QStringLiteral("file"), QUrl::FullyDecoded);
            if (uri.host().compare(QStringLiteral("project"),
                                   Qt::CaseInsensitive) == 0) {
                target.objectId = query.queryItemValue(
                    QStringLiteral("object"), QUrl::FullyDecoded);
            } else if (uri.host().compare(QStringLiteral("register"),
                                          Qt::CaseInsensitive) == 0) {
                target.registerId =
                    uri.path(QUrl::FullyDecoded);
                while (target.registerId.startsWith(QLatin1Char('/'))) {
                    target.registerId.remove(0, 1);
                }
                target.fieldId = query.queryItemValue(
                    QStringLiteral("field"), QUrl::FullyDecoded);
                target.objectId = target.fieldId.isEmpty()
                    ? target.registerId
                    : target.fieldId;
            }
        }
    }

    const QJsonObject arguments =
        params.value(QStringLiteral("arguments")).toObject();
    if (!arguments.value(QStringLiteral("projectPath")).toString().isEmpty())
        target.projectPath =
            arguments.value(QStringLiteral("projectPath")).toString();
    if (!arguments.value(QStringLiteral("objectId")).toString().isEmpty())
        target.objectId =
            arguments.value(QStringLiteral("objectId")).toString();
    if (!arguments.value(QStringLiteral("registerId")).toString().isEmpty())
        target.registerId =
            arguments.value(QStringLiteral("registerId")).toString();
    if (!arguments.value(QStringLiteral("fieldId")).toString().isEmpty())
        target.fieldId =
            arguments.value(QStringLiteral("fieldId")).toString();
    if (!target.fieldId.isEmpty()) {
        target.objectId = target.fieldId;
    } else if (!target.registerId.isEmpty()) {
        target.objectId = target.registerId;
    }
    if (!target.projectPath.isEmpty())
        target.projectPath = QFileInfo(target.projectPath).absoluteFilePath();
    return target;
}

std::optional<ObjectSummary> findFieldObject(
    const std::vector<Field>& fields,
    const QString& objectId,
    const QString& parentId)
{
    for (const Field& field : fields) {
        const QString id = fromUtf8(field.id);
        if (id == objectId)
            return ObjectSummary{id, fromUtf8(field.name),
                                 QStringLiteral("field"), parentId};
        for (const EnumValue& value : field.enumValues) {
            if (fromUtf8(value.id) == objectId) {
                return ObjectSummary{fromUtf8(value.id), fromUtf8(value.name),
                                     QStringLiteral("enum-value"), id};
            }
        }
        if (const auto member = findFieldObject(field.members, objectId, id))
            return member;
    }
    return std::nullopt;
}

std::optional<ObjectSummary> findObject(const Workspace& workspace,
                                        const QString& requestedId)
{
    const QString objectId = requestedId.isEmpty()
        ? fromUtf8(workspace.id) : requestedId;
    if (fromUtf8(workspace.id) == objectId) {
        return ObjectSummary{fromUtf8(workspace.id), fromUtf8(workspace.name),
                             QStringLiteral("workspace"), {}};
    }
    for (const AddressSpace& space : workspace.addressSpaces) {
        const QString spaceId = fromUtf8(space.id);
        if (spaceId == objectId) {
            return ObjectSummary{spaceId, fromUtf8(space.name),
                                 QStringLiteral("address-space"),
                                 fromUtf8(workspace.id)};
        }
        for (const RegisterBlock& block : space.blocks) {
            const QString blockId = fromUtf8(block.id);
            if (blockId == objectId) {
                return ObjectSummary{blockId, fromUtf8(block.name),
                                     QStringLiteral("register-block"), spaceId};
            }
            for (const Register& reg : block.registers) {
                const QString registerId = fromUtf8(reg.id);
                if (registerId == objectId) {
                    return ObjectSummary{registerId, fromUtf8(reg.name),
                                         QStringLiteral("register"), blockId};
                }
                for (const EnumValue& value : reg.enumValues) {
                    if (fromUtf8(value.id) == objectId) {
                        return ObjectSummary{fromUtf8(value.id),
                                             fromUtf8(value.name),
                                             QStringLiteral("enum-value"),
                                             registerId};
                    }
                }
                if (const auto field =
                        findFieldObject(reg.fields, objectId, registerId)) {
                    return field;
                }
            }
        }
    }
    return std::nullopt;
}

QJsonObject diagnosticJson(const Diagnostic& diagnostic)
{
    QString severity = QStringLiteral("error");
    if (diagnostic.severity == DiagnosticSeverity::warning)
        severity = QStringLiteral("warning");
    else if (diagnostic.severity == DiagnosticSeverity::information)
        severity = QStringLiteral("information");
    return {{QStringLiteral("code"), fromUtf8(diagnostic.code)},
            {QStringLiteral("severity"), severity},
            {QStringLiteral("message"), fromUtf8(diagnostic.message)},
            {QStringLiteral("objectId"), fromUtf8(diagnostic.objectId)}};
}

QJsonObject resolvedProject(const ProjectTarget& target,
                            const Workspace* currentWorkspace,
                            QString* failureReason = nullptr)
{
    const ProjectOpenResult opened = currentWorkspace == nullptr
        ? openProject(std::filesystem::path(target.projectPath.toStdWString()))
        : ProjectOpenResult{};
    if (currentWorkspace == nullptr && !opened.workspace.has_value()) {
        if (failureReason) {
            *failureReason = opened.diagnostics.empty()
                ? QStringLiteral("The register-map project could not be opened")
                : fromUtf8(opened.diagnostics.front().message);
        }
        return {};
    }

    const Workspace& workspace = currentWorkspace == nullptr
        ? *opened.workspace
        : *currentWorkspace;
    const std::vector<Diagnostic> currentDiagnostics =
        currentWorkspace == nullptr
        ? std::vector<Diagnostic>{}
        : validateWorkspace(workspace);
    const std::vector<Diagnostic>& modelDiagnostics =
        currentWorkspace == nullptr
        ? opened.diagnostics
        : currentDiagnostics;
    if (!target.registerId.isEmpty()) {
        const Register* reg = regmap::findRegister(
            workspace, target.registerId.toUtf8().toStdString());
        if (reg == nullptr) {
            if (failureReason) {
                *failureReason =
                    QStringLiteral("Register stable ID '%1' was not found")
                        .arg(target.registerId);
            }
            return {};
        }
        if (!target.fieldId.isEmpty()) {
            const auto field = findFieldObject(
                reg->fields, target.fieldId, target.registerId);
            if (!field.has_value() || field->kind != QStringLiteral("field")) {
                if (failureReason) {
                    *failureReason =
                        QStringLiteral(
                            "Field stable ID '%1' was not found under Register '%2'")
                            .arg(target.fieldId, target.registerId);
                }
                return {};
            }
        }
    }
    const std::optional<ObjectSummary> object =
        findObject(workspace, target.objectId);
    if (!object.has_value()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("Stable ID '%1' was not found in the register map")
                    .arg(target.objectId);
        }
        return {};
    }

    int blockCount = 0;
    int registerCount = 0;
    int fieldCount = 0;
    for (const AddressSpace& space : workspace.addressSpaces) {
        blockCount += static_cast<int>(space.blocks.size());
        for (const RegisterBlock& block : space.blocks) {
            registerCount += static_cast<int>(block.registers.size());
            for (const Register& reg : block.registers)
                fieldCount += static_cast<int>(reg.fields.size());
        }
    }

    QJsonArray diagnostics;
    for (const Diagnostic& diagnostic : modelDiagnostics)
        diagnostics.append(diagnosticJson(diagnostic));
    const QJsonObject objectJson{
        {QStringLiteral("id"), object->id},
        {QStringLiteral("name"), object->name},
        {QStringLiteral("kind"), object->kind},
        {QStringLiteral("parentId"), object->parentId},
    };
    return {
        {QStringLiteral("appId"), QStringLiteral("regmap")},
        {QStringLiteral("uri"), target.uri},
        {QStringLiteral("kind"), QStringLiteral("regmap-project")},
        {QStringLiteral("filePath"), target.projectPath},
        {QStringLiteral("title"), fromUtf8(workspace.name)},
        {QStringLiteral("workspaceId"), fromUtf8(workspace.id)},
        {QStringLiteral("registerId"), target.registerId},
        {QStringLiteral("fieldId"), target.fieldId},
        {QStringLiteral("valid"),
         std::ranges::none_of(
             modelDiagnostics,
             [](const Diagnostic& diagnostic) {
                 return diagnostic.severity == DiagnosticSeverity::error;
             })},
        {QStringLiteral("object"), objectJson},
        {QStringLiteral("addressSpaceCount"),
         static_cast<int>(workspace.addressSpaces.size())},
        {QStringLiteral("blockCount"), blockCount},
        {QStringLiteral("registerCount"), registerCount},
        {QStringLiteral("fieldCount"), fieldCount},
        {QStringLiteral("diagnostics"), diagnostics},
    };
}

} // namespace

RegMapSuiteIntegration::RegMapSuiteIntegration(MainWindow* window,
                                               QObject* parent)
    : QObject(parent)
    , window_(window)
{
}

RegMapSuiteIntegration::~RegMapSuiteIntegration() = default;

bool RegMapSuiteIntegration::start(QString* failureReason)
{
    if (provider_ && provider_->isListening())
        return true;
    SuiteApp::RuntimeStartOptions options;
    const SuiteApp::RuntimeStatus runtime = SuiteApp::ensureRuntime(options);
    if (!runtime.available) {
        if (failureReason)
            *failureReason = runtime.errorMessage;
        return false;
    }
    provider_ = std::make_unique<SuiteApp::Provider>(
        appDescriptor(QCoreApplication::applicationVersion()),
        [this](const QJsonObject& request) { return processRequest(request); },
        this);
    return provider_->start(options.endpoint, failureReason);
}

bool RegMapSuiteIntegration::isRegistered() const
{
    return provider_ && provider_->isListening();
}

QJsonObject RegMapSuiteIntegration::appDescriptor(const QString& version,
                                                  const QString& endpoint)
{
    return {
        {QStringLiteral("appId"), QStringLiteral("regmap")},
        {QStringLiteral("displayName"), QStringLiteral("RegMapWorkbench")},
        {QStringLiteral("version"), version.isEmpty()
             ? QStringLiteral("0.0.0") : version},
        {QStringLiteral("processId"),
         static_cast<double>(QCoreApplication::applicationPid())},
        {QStringLiteral("endpoint"), endpoint.isEmpty()
             ? SuiteApp::endpointForApp(QStringLiteral("regmap")) : endpoint},
        {QStringLiteral("protocols"),
         QJsonArray{QString::fromLatin1(SuiteApp::kProtocol)}},
        {QStringLiteral("resourceSchemes"),
         QJsonArray{QStringLiteral("regmap")}},
        {QStringLiteral("actions"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QString::fromLatin1(kOpenAction)},
             {QStringLiteral("resourceSchemes"),
              QJsonArray{QStringLiteral("regmap")}},
             {QStringLiteral("sideEffect"), QStringLiteral("ui")}}}},
        {QStringLiteral("surfaces"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QString::fromLatin1(kWorkbenchSurface)},
             {QStringLiteral("mode"), QStringLiteral("model")},
             {QStringLiteral("fallback"), QStringLiteral("external")}}}},
    };
}

QJsonObject RegMapSuiteIntegration::processRequestForTesting(
    const QJsonObject& request)
{
    return processRequest(request);
}

QJsonObject RegMapSuiteIntegration::processRequest(
    const QJsonObject& request)
{
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();
    const ProjectTarget target = projectTarget(params);
    if (!target.isValid()) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("invalid_resource"),
            QStringLiteral(
                "A regmap://project or regmap://register/<stable-id> URI with a project file is required"));
    }

    QString failureReason;
    const Workspace* currentWorkspace = window_ == nullptr
        ? nullptr
        : window_->currentWorkspaceForSuite(target.projectPath);
    const QJsonObject model = resolvedProject(
        target, currentWorkspace, &failureReason);
    if (model.isEmpty()) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("resource_open_failed"), failureReason);
    }
    if (method == QStringLiteral("resource.resolve"))
        return SuiteApp::successResponse(request, model);

    if (method == QStringLiteral("action.invoke")) {
        if (params.value(QStringLiteral("actionId")).toString()
            != QString::fromLatin1(kOpenAction)) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("action_not_supported"),
                QStringLiteral("Unknown RegMapWorkbench action"));
        }
        const bool opened = window_
            && window_->openStartupProjectPath(target.projectPath,
                                               target.objectId);
        return opened
            ? SuiteApp::successResponse(
                  request, {{QStringLiteral("opened"), true},
                            {QStringLiteral("resource"), model}})
            : SuiteApp::errorResponse(
                  request, QStringLiteral("project_open_failed"),
                  QStringLiteral("RegMapWorkbench could not open the project"));
    }

    if (params.value(QStringLiteral("surfaceId")).toString()
        != QString::fromLatin1(kWorkbenchSurface)) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("surface_not_supported"),
            QStringLiteral("Unknown RegMapWorkbench surface"));
    }
    if (method == QStringLiteral("surface.describe")) {
        return SuiteApp::successResponse(
            request,
            {{QStringLiteral("surfaceId"),
              QString::fromLatin1(kWorkbenchSurface)},
             {QStringLiteral("mode"), QStringLiteral("model")},
             {QStringLiteral("fallback"), QStringLiteral("external")},
             {QStringLiteral("model"), model}});
    }
    if (method == QStringLiteral("surface.open")) {
        const bool opened = window_
            && window_->openStartupProjectPath(target.projectPath,
                                               target.objectId);
        return opened
            ? SuiteApp::successResponse(
                  request, {{QStringLiteral("opened"), true}})
            : SuiteApp::errorResponse(
                  request, QStringLiteral("project_open_failed"),
                  QStringLiteral("RegMapWorkbench could not open the surface"));
    }
    return SuiteApp::errorResponse(
        request, QStringLiteral("method_not_supported"),
        QStringLiteral("RegMapWorkbench does not implement this provider method"));
}

} // namespace regmap::workbench
