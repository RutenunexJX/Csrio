#include "regmap/core/workspace_store.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace {
double privateBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        return static_cast<double>(counters.PrivateUsage);
    }
#endif
    return -1;
}

QJsonObject timings(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return {{"medianMs", values[values.size() / 2]},
            {"p95Ms", values[values.size() * 95 / 100]},
            {"maxMs", values.back()}};
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    constexpr std::size_t registerCount = 1000;
    regmap::Workspace workspace;
    workspace.id = "performance-workspace";
    workspace.name = "History performance";
    regmap::AddressSpace page;
    page.id = "performance-page";
    page.name = "Main";
    page.addressWidth = 32;
    regmap::RegisterBlock block;
    block.id = "performance-block";
    block.name = "Main Block";
    block.size = 0x10000;
    for (std::size_t index = 0; index < registerCount; ++index) {
        regmap::Register reg;
        reg.id = "reg-" + std::to_string(index);
        reg.name = "REGISTER_" + std::to_string(index);
        reg.offset = static_cast<std::uint64_t>(index) * 4;
        reg.width = 32;
        reg.type = regmap::FieldType::structure;
        reg.resetValue = regmap::UnsignedValue(0);
        regmap::Field field;
        field.id = "field-" + std::to_string(index);
        field.name = "VALUE";
        field.type = regmap::FieldType::boolean;
        reg.fields.push_back(std::move(field));
        block.registers.push_back(std::move(reg));
    }
    page.blocks.push_back(std::move(block));
    workspace.addressSpaces.push_back(std::move(page));
    regmap::WorkspaceStore store(std::move(workspace));
    if (!store.diagnostics().empty()) return 3;
    const double initialBytes = privateBytes();
    std::vector<double> edits, undos, redos;
    QElapsedTimer timer;
    for (std::size_t index = 0; index < regmap::WorkspaceStore::historyLimit; ++index) {
        timer.start();
        if (!store.transact("Edit description", [index](regmap::Workspace& candidate) {
                candidate.addressSpaces.front().blocks.front().registers.back().description =
                    "Edit " + std::to_string(index);
            })) return 4;
        edits.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    const double historyBytes = privateBytes();
    for (std::size_t index = 0; index < regmap::WorkspaceStore::historyLimit; ++index) {
        timer.start();
        if (!store.undo()) return 5;
        undos.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    if (store.dirty()) return 6;
    for (std::size_t index = 0; index < regmap::WorkspaceStore::historyLimit; ++index) {
        timer.start();
        if (!store.redo()) return 7;
        redos.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    if (!store.dirty()) return 8;
    QFile output(app.arguments().at(1));
    if (!output.open(QIODevice::WriteOnly)) return 9;
    output.write(QJsonDocument(QJsonObject{
        {"registers", int(registerCount)},
        {"historyEntries", int(store.undoDepth())},
        {"initialPrivateBytes", initialBytes},
        {"historyPrivateBytes", historyBytes},
        {"historyPrivateGrowthBytes", historyBytes - initialBytes},
        {"edit", timings(edits)}, {"undo", timings(undos)}, {"redo", timings(redos)},
        {"measurement", "process private committed memory and synchronous core operations; no GUI"}
    }).toJson());
    return 0;
}
