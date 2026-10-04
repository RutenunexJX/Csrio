#define main regmap_snapshot_main
#include "ui_snapshot.cpp"
#undef main

#include <QAction>
#include <QComboBox>
#include <QClipboard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <functional>

namespace {
void drainDerivedEvents(QWidget* root)
{
    auto* timer = root ? root->findChild<QTimer*>("derivedViewRefreshTimer") : nullptr;
    const auto counters = [root] {
        return root ? QVariantList{root->property("diagnosticsRefreshCount"),
            root->property("diffRefreshCount"), root->property("inlineDiagnosticRowVisitCount")}
            : QVariantList{};
    };
    QElapsedTimer elapsed;
    elapsed.start();
    int stablePasses = 0;
    auto previous = counters();
    // The old implementation uses queued singleShot callbacks instead of the
    // named timer. Observe both versions until two event passes are stable.
    while (stablePasses < 2) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const auto current = counters();
        stablePasses = (!timer || !timer->isActive()) && current == previous ? stablePasses + 1 : 0;
        previous = current;
        if (elapsed.elapsed() > 30000) qFatal("Derived UI did not settle in 30 seconds");
    }
}

QJsonValue observedCounter(QObject* object, const char* name)
{
    const auto value = object->property(name);
    return value.isValid() ? QJsonValue(double(value.toULongLong())) : QJsonValue(QJsonValue::Null);
}

class UiEvents final : public QObject {
public:
    QWidget* root{};
    int paints{};
    int layouts{};
    QMap<QString, int> layoutTargets;
    bool eventFilter(QObject* object, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(object);
        if (root && widget && (widget == root || root->isAncestorOf(widget))) {
            paints += event->type() == QEvent::Paint;
            layouts += event->type() == QEvent::LayoutRequest;
            if (event->type() == QEvent::LayoutRequest) {
                const QString key = QString::fromLatin1(widget->metaObject()->className()) +
                    ':' + widget->objectName();
                ++layoutTargets[key];
            }
        }
        return false;
    }
};

QJsonObject measure(UiEvents& events, const std::function<void(int)>& action)
{
    QJsonArray samples;
    QJsonArray readySamples;
    std::vector<double> elapsed;
    std::vector<double> readyElapsed;
    events.paints = events.layouts = 0;
    events.layoutTargets.clear();
    for (int iteration = 0; iteration < 24; ++iteration) {
        QElapsedTimer timer;
        timer.start();
        action(iteration);
        const double dispatch = double(timer.nsecsElapsed()) / 1000000.0;
        elapsed.push_back(dispatch);
        drainDerivedEvents(events.root);
        const double ready = double(timer.nsecsElapsed()) / 1000000.0;
        readyElapsed.push_back(ready);
        readySamples.append(ready);
        QTest::qWait(350);
        samples.append(dispatch);
    }
    std::sort(elapsed.begin(), elapsed.end());
    std::sort(readyElapsed.begin(), readyElapsed.end());
    QJsonObject layoutTargets;
    for (auto it = events.layoutTargets.cbegin(); it != events.layoutTargets.cend(); ++it)
        layoutTargets.insert(it.key(), it.value());
    return {{"dispatchMs", samples}, {"medianDispatchMs", elapsed[12]},
            {"p95DispatchMs", elapsed[22]}, {"maxDispatchMs", elapsed.back()},
            {"readyMs", readySamples}, {"medianReadyMs", readyElapsed[12]},
            {"p95ReadyMs", readyElapsed[22]}, {"maxReadyMs", readyElapsed.back()},
            {"paintEvents", events.paints}, {"layoutEvents", events.layouts},
            {"layoutTargets", layoutTargets}};
}
}

#ifndef REGMAP_PERFORMANCE_NO_MAIN
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (app.arguments().size() < 2 || app.arguments().size() > 4) return 2;
    const QString scenario = app.arguments().size() > 2 ? app.arguments().at(2) : "all";
    bool countValid = true;
    const int registerCount = app.arguments().size() > 3 ? app.arguments().at(3).toInt(&countValid) : 1000;
    if (!countValid || registerCount < 7 || registerCount > 10000 ||
        !QStringList{"all", "panel", "search", "search-exact", "edit", "paste", "open"}.contains(scenario)) return 2;
    QCoreApplication::setOrganizationName("RegMapPerformance");
    QCoreApplication::setApplicationName("RegMapPerformance");
    QTemporaryDir profile, project;
    if (!profile.isValid() || !project.isValid()) return 3;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    WorkbenchTheme::apply(app, WorkbenchTheme::Mode::light);
    const auto path = project.filePath("performance.regmap.yaml");
    if (!createFixture(path)) return 4;
    {
        // The fixture builder must stop watching before measured windows open it.
        ProjectController creator;
        if (!creator.openProject(path)) return 5;
        if (!creator.editWorkspace("Performance workload", [registerCount](regmap::Workspace& workspace) {
            auto& block = workspace.addressSpaces.front().blocks.front();
            block.size = 0x10000;
            for (int i = 6; i < registerCount; ++i) {
                block.registers.push_back(makeScalarRegister("perf-" + std::to_string(i),
                    "PERF_" + std::to_string(i), std::uint64_t(i) * 4,
                    regmap::AccessMode::readWrite, "Deterministic performance register"));
            }
        })) return 6;
        creator.save();
        if (creator.isDirty() || creator.hasProjectErrors()) return 7;
    }
    UiEvents events;
    app.installEventFilter(&events);
    QJsonArray runs;
    QJsonArray opens;
    for (const QSize size : {QSize(960, 720), QSize(1440, 900)}) {
        QSettings().clear();
        QElapsedTimer openTimer;
        openTimer.start();
        MainWindow window;
        const double constructionMs = double(openTimer.nsecsElapsed()) / 1e6;
        openTimer.restart();
        if (!window.openProjectPath(path)) return 8;
        const double openDispatchMs = double(openTimer.nsecsElapsed()) / 1e6;
        drainDerivedEvents(&window);
        const double openReadyMs = double(openTimer.nsecsElapsed()) / 1e6;
        window.resize(size); window.show(); QTest::qWait(50);
        window.resize(size); QTest::qWait(50);
        if (window.size() != size || app.font().pointSizeF() != 10.0) return 9;
        auto* results = window.findChild<QAction*>("toggleResultsAction");
        auto* table = window.findChild<QTableView*>("registerView");
        if (!results || !results->isEnabled() || !table) return 10;
        auto* controller = window.findChild<ProjectController*>();
        if (!controller) return 13;
        opens.append(QJsonObject{{"registers", registerCount}, {"width", size.width()},
            {"constructionMs", constructionMs}, {"openDispatchMs", openDispatchMs}, {"openReadyMs", openReadyMs},
            {"loadCountersAvailable", controller->property("projectFileReadCount").isValid()},
            {"fileReads", observedCounter(controller, "projectFileReadCount")},
            {"yamlParses", observedCounter(controller, "projectYamlParseCount")},
            {"modelValidations", observedCounter(controller, "projectValidationCount")}});
        QJsonObject phases;
        for (const char* name : {"phaseLoadSnapshotMs", "phaseAdoptStoreMs", "phaseSynchronizationMs",
                 "phaseSyncLoadBaselineMs", "phaseSyncParseRtlMs", "phaseSyncMergeMs",
                 "phaseSyncAdoptMergeMs", "phaseSyncPersistMs", "phaseSyncSaveProjectMs",
                 "phaseSyncWriteRtlMs", "phaseSyncSaveBaselineMs", "phaseSyncGenerateMs"}) {
            const auto value = controller->property(name);
            phases.insert(name, value.isValid() ? QJsonValue(value.toDouble()) : QJsonValue(QJsonValue::Null));
        }
        for (const char* name : {"phaserefreshProjectMs", "phasepopulateHierarchyMs",
                 "phasepopulateRegistersMs", "phasepopulateFieldsMs"}) {
            const auto value = window.property(name);
            phases.insert(name, value.isValid() ? QJsonValue(value.toDouble()) : QJsonValue(QJsonValue::Null));
        }
        auto openResult = opens.last().toObject();
        openResult.insert("phases", phases);
        openResult.insert("hasProjectErrors", controller->hasProjectErrors());
        openResult.insert("dirty", controller->isDirty());
        QJsonArray errors;
        for (const auto& diagnostic : controller->diagnostics()) {
            if (diagnostic.severity == regmap::DiagnosticSeverity::error) {
                errors.append(QJsonObject{{"code", QString::fromStdString(diagnostic.code)},
                    {"message", QString::fromStdString(diagnostic.message)}});
            }
        }
        openResult.insert("errors", errors);
        opens.replace(opens.size() - 1, openResult);
        events.root = &window;
        if (scenario == "all" || scenario == "panel") {
            auto result = measure(events, [results](int) { results->trigger(); });
            result.insert("scenario", "results-panel-12-cycles");
            result.insert("width", size.width()); result.insert("height", size.height());
            result.insert("dpr", window.devicePixelRatioF());
            result.insert("registers", registerCount);
            if (auto* panel = window.findChild<QWidget*>("resultsPanel")) {
                result.insert("peakSnapshotBytes", double(panel->property("regmapPanelPeakSnapshotBytes").toLongLong()));
                result.insert("lastCapturePreparationMs", panel->property("regmapPanelPreparationMs").toDouble());
                result.insert("retainedSnapshotBytes", double(panel->property("regmapPanelSnapshotBytes").toLongLong()));
            }
            runs.append(result);
        }
        auto* search = window.findChild<QLineEdit*>("globalSearchEdit");
        if (!search) return 11;
        if (scenario == "all" || scenario == "search" || scenario == "search-exact") {
            auto result = measure(events, [search, scenario](int i) {
                search->setText(scenario == "search-exact"
                    ? (i % 2 ? "rw" : "no_such_register") : (i % 2 ? "PERF_99" : "PERF_"));
            });
            result.insert("scenario", scenario == "search-exact" ? "search-exact-24-edits" : "search-24-edits");
            result.insert("width", size.width()); result.insert("height", size.height());
            result.insert("dpr", window.devicePixelRatioF()); result.insert("registers", registerCount);
            runs.append(result);
            search->clear();
        }
        if (scenario == "paste") {
            auto* paste = window.findChild<QAction*>("pasteSelectionAction");
            if (!paste) return 16;
            const int cells = std::min(registerCount, 100);
            const auto validationCount = controller->modelValidationCount();
            const auto depth = controller->undoDepth();
            auto result = measure(events, [&](int iteration) {
                table->setCurrentIndex(table->model()->index(0, 11));
                table->selectionModel()->clearSelection();
                for (int r = 0; r < cells; ++r)
                    table->selectionModel()->select(table->model()->index(r, 11), QItemSelectionModel::Select);
                table->setFocus();
                QApplication::clipboard()->setText(QStringLiteral("Batch description %1").arg(iteration));
                paste->trigger();
            });
            if (table->model()->index(cells - 1, 11).data().toString() != "Batch description 23") return 17;
            result.insert("scenario", "paste-100-cells-24-edits");
            result.insert("registers", registerCount);
            result.insert("width", size.width()); result.insert("height", size.height());
            result.insert("cellsPerPaste", cells);
            result.insert("modelValidations", double(controller->modelValidationCount() - validationCount));
            result.insert("undoDepthIncrease", double(controller->undoDepth() - depth));
            result.insert("lastPasteFullStateComparisons", observedCounter(&window, "pasteFullStateComparisons"));
            result.insert("estimatedHistoryBytes", double(controller->historyBytes()));
            result.insert("historyTrimCount", double(controller->historyTrimCount()));
            runs.append(result);
        }
        if (scenario == "edit") {
            const QString id = QStringLiteral("perf-%1").arg(registerCount - 1);
            int row = -1;
            for (int index = 0; index < table->model()->rowCount(); ++index)
                if (table->model()->index(index, 0).data(Qt::UserRole + 1).toString() == id) row = index;
            if (row < 0) return 14;
            const QStringList counterNames{"inlineDiagnosticRowVisitCount", "inlineDiagnosticCellUpdateCount",
                "diagnosticsRefreshCount", "diffRefreshCount", "fullTableRefreshCount", "incrementalTableRefreshCount"};
            QMap<QString, qulonglong> before;
            for (const auto& name : counterNames) before[name] = window.property(name.toUtf8().constData()).toULongLong();
            bool editsAccepted = true;
            auto result = measure(events, [&](int iteration) {
                editsAccepted &= table->model()->setData(table->model()->index(row, 11),
                    QStringLiteral("Measured description %1").arg(iteration));
            });
            if (!editsAccepted) return 15;
            result.insert("derivedCountersAvailable", window.property("diffRefreshCount").isValid());
            for (const auto& name : counterNames) {
                const auto value = window.property(name.toUtf8().constData());
                result.insert(name, value.isValid() ? QJsonValue(double(value.toULongLong() - before[name]))
                                                   : QJsonValue(QJsonValue::Null));
            }
            result.insert("scenario", "description-24-edits");
            result.insert("estimatedHistoryBytes", double(controller->historyBytes()));
            result.insert("historyTrimCount", double(controller->historyTrimCount()));
            result.insert("registers", registerCount);
            result.insert("width", size.width()); result.insert("height", size.height());
            runs.append(result);
        }
        events.root = nullptr;
    }
    QFile output(app.arguments().at(1));
    if (!output.open(QIODevice::WriteOnly)) return 12;
    output.write(QJsonDocument(QJsonObject{{"runs", runs}, {"opens", opens},
        {"measurement", "dispatch measures synchronous calls; ready/openReady drain both old queued callbacks and the new derived timer; openReady is before show, not first visible frame; unavailable counters are null; offscreen is not desktop frame rate"}}).toJson());
    return 0;
}
#endif
