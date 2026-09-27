#include "main_window.hpp"
#include "project_controller.hpp"
#include "workbench_theme.hpp"
#include "workbench_controls.hpp"

#include "regmap/core/model.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFontInfo>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QImage>
#include <QModelIndex>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace {

struct Options {
    QString output;
    WorkbenchTheme::Mode theme{
        WorkbenchTheme::Mode::light};
    int width{1440};
    int height{900};
};

[[nodiscard]] std::optional<Options>
parseOptions(const QStringList& arguments)
{
    Options options;
    for (int index = 1;
         index < arguments.size(); ++index) {
        const QString argument =
            arguments[index];
        const auto takeValue =
            [&arguments, &index]()
                -> std::optional<QString> {
                if (index + 1 >=
                    arguments.size()) {
                    return std::nullopt;
                }
                ++index;
                return arguments[index];
            };
        if (argument ==
            QStringLiteral("--output")) {
            const auto value = takeValue();
            if (!value) {
                return std::nullopt;
            }
            options.output = *value;
        } else if (argument ==
                   QStringLiteral("--theme")) {
            const auto value = takeValue();
            if (!value ||
                (*value != QStringLiteral("light") &&
                 *value != QStringLiteral("dark"))) {
                return std::nullopt;
            }
            options.theme =
                *value == QStringLiteral("dark")
                    ? WorkbenchTheme::Mode::dark
                    : WorkbenchTheme::Mode::light;
        } else if (argument ==
                   QStringLiteral("--width")) {
            const auto value = takeValue();
            bool ok = false;
            options.width =
                value ? value->toInt(&ok) : 0;
            if (!ok || options.width < 640) {
                return std::nullopt;
            }
        } else if (argument ==
                   QStringLiteral("--height")) {
            const auto value = takeValue();
            bool ok = false;
            options.height =
                value ? value->toInt(&ok) : 0;
            if (!ok || options.height < 480) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (options.output.isEmpty()) {
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] regmap::Field makeField(
    std::string id,
    std::string name,
    const std::uint32_t msb,
    const std::uint32_t lsb,
    const regmap::AccessMode softwareAccess,
    const regmap::AccessMode hardwareAccess,
    std::string description,
    const regmap::WriteSideEffect writeEffect =
        regmap::WriteSideEffect::write)
{
    regmap::Field field;
    field.id = std::move(id);
    field.name = std::move(name);
    field.msb = msb;
    field.lsb = lsb;
    field.type = msb == lsb
        ? regmap::FieldType::boolean
        : regmap::FieldType::bits;
    field.softwareAccess = softwareAccess;
    field.hardwareAccess = hardwareAccess;
    field.writeSideEffect = writeEffect;
    field.description =
        std::move(description);
    return field;
}

[[nodiscard]] regmap::Register makeScalarRegister(
    std::string id,
    std::string name,
    const std::uint64_t offset,
    const regmap::AccessMode access,
    std::string description)
{
    regmap::Register reg;
    reg.id = std::move(id);
    reg.name = std::move(name);
    reg.offset = offset;
    reg.width = 32;
    reg.type =
        regmap::FieldType::unsignedInteger;
    reg.access = access;
    reg.resetValue =
        regmap::UnsignedValue{0};
    reg.description =
        std::move(description);
    return reg;
}

[[nodiscard]] bool createFixture(
    const QString& manifestPath)
{
    ProjectController creator;
    if (!creator.createProject(
            manifestPath)) {
        return false;
    }
    const bool edited =
        creator.editWorkspace(
            QStringLiteral(
                "Create deterministic UI fixture"),
            [](regmap::Workspace& workspace) {
                workspace.name =
                    "Aurora Telemetry Controller";
                regmap::AddressSpace page;
                page.id = "space-telemetry";
                page.name = "Telemetry Fabric";
                page.baseAddress = 0x43C00000;
                page.addressWidth = 32;
                page.description =
                    "Control, status, and event telemetry registers.";

                regmap::RegisterBlock block;
                block.id = "block-control";
                block.name = "Control & Status";
                block.baseAddress = 0;
                block.size = 0x1000;
                block.description =
                    "Host-visible control and live telemetry state.";

                regmap::Register control =
                    makeScalarRegister(
                        "reg-control", "CONTROL",
                        0x00,
                        regmap::AccessMode::readWrite,
                        "Global enable, mode, and acquisition controls.");
                control.type =
                    regmap::FieldType::structure;
                control.initialValue =
                    regmap::UnsignedValue{1};
                control.resetValue =
                    regmap::UnsignedValue{1};
                control.tags =
                    {"control", "critical"};
                control.fields = {
                    makeField(
                        "field-enable", "ENABLE",
                        0, 0,
                        regmap::AccessMode::readWrite,
                        regmap::AccessMode::readOnly,
                        "Enables acquisition."),
                    makeField(
                        "field-mode", "MODE",
                        3, 1,
                        regmap::AccessMode::readWrite,
                        regmap::AccessMode::readOnly,
                        "Selects the acquisition profile."),
                    makeField(
                        "field-trigger", "SOFT_TRIGGER",
                        4, 4,
                        regmap::AccessMode::writeOnly,
                        regmap::AccessMode::readOnly,
                        "Starts one software-triggered capture."),
                    makeField(
                        "field-watermark", "WATERMARK",
                        15, 8,
                        regmap::AccessMode::readWrite,
                        regmap::AccessMode::readOnly,
                        "FIFO watermark threshold."),
                };

                regmap::Register status =
                    makeScalarRegister(
                        "reg-status", "STATUS",
                        0x04,
                        regmap::AccessMode::readOnly,
                        "Live acquisition and FIFO status.");
                status.type =
                    regmap::FieldType::structure;
                status.addressFixed = true;
                status.tags =
                    {"status"};
                status.fields = {
                    makeField(
                        "field-ready", "READY",
                        0, 0,
                        regmap::AccessMode::readOnly,
                        regmap::AccessMode::writeOnly,
                        "Hardware is ready.",
                        regmap::WriteSideEffect::none),
                    makeField(
                        "field-busy", "BUSY",
                        1, 1,
                        regmap::AccessMode::readOnly,
                        regmap::AccessMode::writeOnly,
                        "Acquisition is active.",
                        regmap::WriteSideEffect::none),
                    makeField(
                        "field-level", "FIFO_LEVEL",
                        15, 8,
                        regmap::AccessMode::readOnly,
                        regmap::AccessMode::writeOnly,
                        "Current FIFO fill level.",
                        regmap::WriteSideEffect::none),
                };

                regmap::Register events =
                    makeScalarRegister(
                        "reg-events", "EVENT_STATUS",
                        0x08,
                        regmap::AccessMode::readWrite,
                        "Latched event flags; write one to clear.");
                events.type =
                    regmap::FieldType::structure;
                events.tags =
                    {"interrupt", "status"};
                events.fields = {
                    makeField(
                        "field-events", "PENDING",
                        7, 0,
                        regmap::AccessMode::readWrite,
                        regmap::AccessMode::writeOnly,
                        "Pending event sources.",
                        regmap::WriteSideEffect::oneToClear),
                };

                block.registers = {
                    std::move(control),
                    std::move(status),
                    std::move(events),
                    makeScalarRegister(
                        "reg-sample-count", "SAMPLE_COUNT",
                        0x0C,
                        regmap::AccessMode::readOnly,
                        "Number of accepted samples."),
                    makeScalarRegister(
                        "reg-timebase", "TIMEBASE_DIV",
                        0x10,
                        regmap::AccessMode::readWrite,
                        "Timebase divider for acquisition."),
                    makeScalarRegister(
                        "reg-build", "BUILD_ID",
                        0x14,
                        regmap::AccessMode::readOnly,
                        "Immutable hardware build identifier."),
                };
                page.blocks.push_back(
                    std::move(block));
                workspace.addressSpaces = {
                    std::move(page)};
            });
    if (!edited) {
        return false;
    }
    creator.save();
    return creator.workspace() != nullptr &&
        !creator.isDirty() &&
        !creator.hasProjectErrors();
}

} // namespace

int main(int argumentCount, char* arguments[])
{
    QApplication application(
        argumentCount, arguments);
    QCoreApplication::setOrganizationName(
        QStringLiteral(
            "RegMapWorkbenchSnapshot"));
    QCoreApplication::setApplicationName(
        QStringLiteral(
            "RegMapWorkbenchSnapshot"));
    const auto options =
        parseOptions(
            QCoreApplication::arguments());
    if (!options) {
        return 2;
    }

    QTemporaryDir settingsDirectory;
    QTemporaryDir projectDirectory;
    if (!settingsDirectory.isValid() ||
        !projectDirectory.isValid()) {
        return 3;
    }
    QSettings::setDefaultFormat(
        QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        settingsDirectory.path());
    WorkbenchTheme::apply(
        application,
        options->theme);
    if (!QFontMetrics(application.font())
             .inFontUcs4('A')) {
        return 10;
    }
    const auto codeFont = WorkbenchTheme::monospaceFont();
    const QFontMetrics codeMetrics(codeFont);
    if (!codeMetrics.inFontUcs4('0') || !codeMetrics.inFontUcs4('x') ||
        codeMetrics.horizontalAdvance('i') != codeMetrics.horizontalAdvance('W') ||
        application.font().pointSizeF() != 10.0 || codeFont.pointSizeF() != 10.0) {
        QTextStream(stderr) << "Unreadable or non-monospace numeric font\n";
        return 12;
    }
    QTextStream(stderr) << "fonts ui=" << QFontInfo(application.font()).family()
                        << " numeric=" << QFontInfo(codeFont).family() << " points=10\n";

    const QString manifest =
        projectDirectory.filePath(
            QStringLiteral(
                "aurora.regmap.yaml"));
    if (!createFixture(manifest)) {
        return 4;
    }

    MainWindow window;
    if (!window.openProjectPath(manifest)) {
        return 5;
    }
    window.resize(
        options->width,
        options->height);
    window.show();
    QCoreApplication::processEvents();
    window.resize(
        options->width,
        options->height);
    QCoreApplication::processEvents();

    QTextStream diagnostic(stderr);
    diagnostic
        << QStringLiteral(
               "snapshot requested=%1x%2 actual=%3x%4 minimumHint=%5x%6\n")
               .arg(options->width)
               .arg(options->height)
               .arg(window.width())
               .arg(window.height())
               .arg(window.minimumSizeHint().width())
               .arg(window.minimumSizeHint().height());
    diagnostic.flush();
    if (window.size() !=
        QSize(options->width,
              options->height)) {
        return 8;
    }

    int result = 6;
    QTimer::singleShot(
        180, &window,
        [&application, &window, &result,
         output = options->output,
         showResults = options->width >= 1200] {
            auto* pageHeader =
                window.findChild<QWidget*>(
                    QStringLiteral(
                        "pageHeader"));
            const auto criticalControlIsVisible =
                [pageHeader, &window](
                    const QString& objectName,
                    const int minimumWidth) {
                    QWidget* control =
                        window.findChild<QWidget*>(
                            objectName);
                    if (pageHeader == nullptr ||
                        control == nullptr ||
                        !control->isVisibleTo(
                            &window) ||
                        control->width() <
                            minimumWidth) {
                        QTextStream(stderr)
                            << QStringLiteral(
                                   "critical control failed: %1 visible=%2 width=%3 minimum=%4\n")
                                   .arg(objectName)
                                   .arg(
                                       control != nullptr &&
                                       control->isVisibleTo(
                                           &window))
                                   .arg(
                                       control == nullptr
                                           ? -1
                                           : control->width())
                                   .arg(minimumWidth);
                        return false;
                    }
                    const QRect bounds(
                        control->mapTo(
                            pageHeader,
                            QPoint{0, 0}),
                        control->size());
                    const bool contained =
                        pageHeader->rect()
                            .contains(bounds);
                    if (!contained) {
                        QTextStream(stderr)
                            << QStringLiteral(
                                   "critical control outside header: %1 bounds=%2,%3 %4x%5 header=%6x%7\n")
                                   .arg(objectName)
                                   .arg(bounds.x())
                                   .arg(bounds.y())
                                   .arg(bounds.width())
                                   .arg(bounds.height())
                                   .arg(pageHeader->width())
                                   .arg(pageHeader->height());
                    }
                    return contained;
                };
            if (!criticalControlIsVisible(
                    QStringLiteral(
                        "projectTitleLabel"),
                    96) ||
                !criticalControlIsVisible(
                    QStringLiteral(
                        "syncStateBadge"),
                    60) ||
                (showResults &&
                 !criticalControlIsVisible(
                     QStringLiteral(
                         "syncStateBadge"),
                     70)) ||
                !criticalControlIsVisible(
                    QStringLiteral(
                        "moreProjectButton"),
                    40) ||
                !criticalControlIsVisible(
                    QStringLiteral(
                        "saveSyncButton"),
                    104)) {
                result = 9;
                application.exit(result);
                return;
            }
            if (auto* registers =
                    window.findChild<QTableView*>(
                        QStringLiteral(
                            "registerView"));
                registers != nullptr &&
                registers->model() != nullptr &&
                registers->model()->rowCount() > 0) {
                const QModelIndex fields =
                    registers->model()->index(
                        0, 5);
                registers->setCurrentIndex(fields);
                Q_EMIT registers->clicked(fields);
            }
            if (auto* controller =
                    window.findChild<ProjectController*>()) {
                static_cast<void>(
                    controller->editWorkspace(
                        QStringLiteral(
                            "Preview unsaved competition edit"),
                        [](regmap::Workspace& workspace) {
                            if (auto* reg =
                                    regmap::findRegister(
                                        workspace,
                                        "reg-control")) {
                                reg->description =
                                    "Global enable, mode, acquisition controls, and the pending competition edit.";
                            }
                        }));
            }
            if (auto* toggle =
                    window.findChild<QToolButton*>(
                        QStringLiteral(
                            "resultsToggleButton"));
                showResults &&
                toggle != nullptr &&
                toggle->isEnabled() &&
                !toggle->isChecked()) {
                toggle->click();
            }
            if (auto* tabs =
                    window.findChild<QTabWidget*>(
                        QStringLiteral(
                            "resultTabs"));
                showResults &&
                tabs != nullptr &&
                tabs->isTabVisible(2)) {
                tabs->setCurrentIndex(2);
            }
            QCoreApplication::processEvents();
            QTimer::singleShot(
                // Capture the final layout after the 300 ms drawer transition.
                400, &window,
                [&application, &window,
                 &result, output,
                 pageHeader, expectedSize = window.size()] {
                    if (window.size() != expectedSize) {
                        result = 8; application.exit(result); return;
                    }
                    if (WorkbenchControls::backend() == WorkbenchControls::Backend::ela) {
                        for (const auto* name : {"registerPanelScroll", "fieldPanelScroll"}) {
                            auto* scroll = window.findChild<QScrollArea*>(name);
                            if (scroll && !scroll->isVisible()) continue;
                            if (!scroll || !scroll->widget() ||
                                scroll->widget()->height() < scroll->widget()->minimumSizeHint().height() ||
                                scroll->widget()->width() < scroll->widget()->minimumSizeHint().width()) {
                                QTextStream(stderr) << "Panel content clipped: " << name
                                    << " content=" << (scroll && scroll->widget() ? scroll->widget()->width() : -1)
                                    << 'x' << (scroll && scroll->widget() ? scroll->widget()->height() : -1)
                                    << " minimum=" << (scroll && scroll->widget() ? scroll->widget()->minimumSizeHint().width() : -1)
                                    << 'x' << (scroll && scroll->widget() ? scroll->widget()->minimumSizeHint().height() : -1)
                                    << " visible=" << (scroll && scroll->isVisible()) << '\n';
                                result = 13; application.exit(result); return;
                            }
                            if (scroll->widget()->height() > scroll->viewport()->height() &&
                                scroll->verticalScrollBar()->maximum() <= 0) {
                                result = 14; application.exit(result); return;
                            }
                        }
                        auto* bitfield = window.findChild<QWidget*>("bitfieldView");
                        if (!bitfield || bitfield->height() < 188) {
                            result = 15; application.exit(result); return;
                        }
                        auto* registers = window.findChild<QTableView*>("registerView");
                        if (!registers || (registers->isVisible() && (registers->height() < 104 ||
                            !registers->parentWidget()->rect().contains(registers->geometry()) ||
                            registers->viewport()->height() < registers->verticalHeader()->defaultSectionSize()))) {
                            QTextStream(stderr) << "Register table lost its visible data row\n";
                            result = 16; application.exit(result); return;
                        }
                    }
                    const QStringList headerControls{
                        QStringLiteral(
                            "projectTitleLabel"),
                        QStringLiteral(
                            "fileStateBadge"),
                        QStringLiteral(
                            "syncStateBadge"),
                        QStringLiteral(
                            "generateButton"),
                        QStringLiteral(
                            "synchronizeButton"),
                        QStringLiteral(
                            "saveSyncButton")};
                    QList<QRect> occupied;
                    for (const QString& objectName :
                         headerControls) {
                        QWidget* control =
                            window.findChild<QWidget*>(
                                objectName);
                        if (control == nullptr ||
                            !control->isVisibleTo(
                                &window)) {
                            continue;
                        }
                        const QRect bounds(
                            control->mapTo(
                                pageHeader,
                                QPoint{0, 0}),
                            control->size());
                        for (const QRect& prior :
                             occupied) {
                            if (prior.intersects(
                                    bounds)) {
                                QTextStream(stderr)
                                    << QStringLiteral(
                                           "header controls overlap after state update: %1\n")
                                           .arg(
                                               objectName);
                                result = 11;
                                application.exit(
                                    result);
                                return;
                            }
                        }
                        occupied.push_back(
                            bounds);
                    }
                    const QFileInfo target(output);
                    QDir{}.mkpath(
                        target.absolutePath());
                    const QImage image =
                        window.grab().toImage();
                    result =
                        !image.isNull() &&
                                image.save(output, "PNG")
                            ? 0
                            : 7;
                    application.exit(result);
                });
        });
    application.exec();
    return result;
}
