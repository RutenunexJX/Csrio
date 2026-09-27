#define main regmap_snapshot_main
#include "ui_snapshot.cpp"
#undef main

#include <QAction>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QTest>
#include <algorithm>
#include <functional>

namespace {
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
    std::vector<double> elapsed;
    events.paints = events.layouts = 0;
    events.layoutTargets.clear();
    for (int iteration = 0; iteration < 24; ++iteration) {
        QElapsedTimer timer;
        timer.start();
        action(iteration);
        const double dispatch = double(timer.nsecsElapsed()) / 1000000.0;
        elapsed.push_back(dispatch);
        QTest::qWait(350);
        samples.append(dispatch);
    }
    std::sort(elapsed.begin(), elapsed.end());
    QJsonObject layoutTargets;
    for (auto it = events.layoutTargets.cbegin(); it != events.layoutTargets.cend(); ++it)
        layoutTargets.insert(it.key(), it.value());
    return {{"dispatchMs", samples}, {"medianDispatchMs", elapsed[12]},
            {"p95DispatchMs", elapsed[22]}, {"maxDispatchMs", elapsed.back()},
            {"paintEvents", events.paints}, {"layoutEvents", events.layouts},
            {"layoutTargets", layoutTargets}};
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    QCoreApplication::setOrganizationName("RegMapPerformance");
    QCoreApplication::setApplicationName("RegMapPerformance");
    QTemporaryDir profile, project;
    if (!profile.isValid() || !project.isValid()) return 3;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    WorkbenchTheme::apply(app, WorkbenchTheme::Mode::light);
    const auto path = project.filePath("performance.regmap.yaml");
    if (!createFixture(path)) return 4;
    ProjectController creator;
    if (!creator.openProject(path)) return 5;
    if (!creator.editWorkspace("Performance workload", [](regmap::Workspace& workspace) {
        auto& block = workspace.addressSpaces.front().blocks.front();
        block.size = 0x10000;
        for (int i = 6; i < 1000; ++i) {
            block.registers.push_back(makeScalarRegister("perf-" + std::to_string(i),
                "PERF_" + std::to_string(i), std::uint64_t(i) * 4,
                regmap::AccessMode::readWrite, "Deterministic performance register"));
        }
    })) return 6;
    creator.save();
    if (creator.isDirty() || creator.hasProjectErrors()) return 7;
    UiEvents events;
    app.installEventFilter(&events);
    QJsonArray runs;
    for (const QSize size : {QSize(960, 720), QSize(1440, 900)}) {
        QSettings().clear();
        MainWindow window;
        if (!window.openProjectPath(path)) return 8;
        window.resize(size); window.show(); QTest::qWait(50);
        window.resize(size); QTest::qWait(50);
        if (window.size() != size || app.font().pointSizeF() != 10.0) return 9;
        auto* results = window.findChild<QAction*>("toggleResultsAction");
        auto* table = window.findChild<QTableView*>("registerView");
        if (!results || !results->isEnabled() || !table) return 10;
        events.root = &window;
        auto result = measure(events, [results](int) { results->trigger(); });
        result.insert("scenario", "results-panel-12-cycles");
        result.insert("width", size.width()); result.insert("height", size.height());
        result.insert("dpr", window.devicePixelRatioF());
        result.insert("registers", 1000);
        if (auto* panel = window.findChild<QWidget*>("resultsPanel")) {
            result.insert("peakSnapshotBytes", double(panel->property("regmapPanelPeakSnapshotBytes").toLongLong()));
            result.insert("lastCapturePreparationMs", panel->property("regmapPanelPreparationMs").toDouble());
            result.insert("retainedSnapshotBytes", double(panel->property("regmapPanelSnapshotBytes").toLongLong()));
        }
        runs.append(result);
        auto* search = window.findChild<QLineEdit*>("globalSearchEdit");
        if (!search) return 11;
        result = measure(events, [search](int i) {
            search->setText(i % 2 ? "PERF_99" : "PERF_");
        });
        result.insert("scenario", "search-24-edits");
        result.insert("width", size.width()); result.insert("height", size.height());
        result.insert("dpr", window.devicePixelRatioF()); result.insert("registers", 1000);
        runs.append(result);
        search->clear();
        events.root = nullptr;
    }
    QFile output(app.arguments().at(1));
    if (!output.open(QIODevice::WriteOnly)) return 12;
    output.write(QJsonDocument(QJsonObject{{"runs", runs},
        {"measurement", "offscreen dispatch/layout/paint; not display frame rate"}}).toJson());
    return 0;
}
