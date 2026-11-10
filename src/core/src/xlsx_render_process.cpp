#include "regmap/core/xlsx_export.hpp"
#include "regmap/core/three_way_merge.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace regmap {
namespace {
XlsxExportResult failure(const QString& stage, const QString& path, const QString& reason)
{
    XlsxExportResult result;
    Diagnostic diagnostic;
    diagnostic.code = "RM4100";
    const auto recovery = stage == "cancel" || stage == "timeout"
        ? QStringLiteral("Retry generation for the current project snapshot.")
        : QStringLiteral("Restore the matching renderer, resources/xlsx font and Qt offscreen plugin "
                         "from the same Csrio installation, then retry generation.");
    diagnostic.message = QStringLiteral("XLSX renderer %1 failed [%2]: %3. "
        "Existing output was retained. %4").arg(stage, path, reason, recovery).toStdString();
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
}
}

XlsxExportResult exportReadOnlyWorkbook(const Workspace& workspace, bool consecutiveEnums,
                                       std::stop_token cancellation)
{
    const QString renderer = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("regmap-xlsx-render.exe"));
#else
        QStringLiteral("regmap-xlsx-render"));
#endif
    if (cancellation.stop_requested()) return failure("cancel", renderer, "request cancelled");
    if (!QFileInfo(renderer).isFile()) return failure("locate", renderer, "executable is missing");

    const auto snapshot = serializeWorkspaceState(workspace, false);
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    // The parent may be a native GUI, a headless CLI, or an embedded workbench.
    // None of its Qt platform, font, scale or plugin overrides defines this render.
    for (const QString& key : environment.keys())
        if (key.startsWith(QStringLiteral("QT_"))) environment.remove(key);
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("QT_QPA_OFFSCREEN_NO_GLX", "1");
    environment.insert("QT_FONT_DPI", "96");
    environment.insert("QT_SCALE_FACTOR", "1");
    environment.insert("QT_QPA_FONTDIR", QDir(QCoreApplication::applicationDirPath())
        .filePath("resources/xlsx"));
    process.setProcessEnvironment(environment);
    process.start(renderer, {consecutiveEnums ? QStringLiteral("consecutive") : QStringLiteral("full")});
    if (!process.waitForStarted(5000)) return failure("start", renderer, process.errorString());
    if (process.write(snapshot.data(), static_cast<qint64>(snapshot.size())) != static_cast<qint64>(snapshot.size())) {
        process.kill();
        process.waitForFinished(5000);
        return failure("input", renderer, process.errorString());
    }
    process.closeWriteChannel();
    QElapsedTimer elapsed;
    elapsed.start();
    while (!process.waitForFinished(50)) {
        if (cancellation.stop_requested() || elapsed.elapsed() >= 120000) {
            process.kill();
            process.waitForFinished(5000);
            return failure(cancellation.stop_requested() ? "cancel" : "timeout", renderer,
                cancellation.stop_requested() ? "request cancelled" : "120 second limit exceeded");
        }
        if (process.state() == QProcess::NotRunning) break;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return failure("render", renderer, QStringLiteral("exit %1: %2")
            .arg(process.exitCode()).arg(QString::fromUtf8(process.readAllStandardError()).trimmed()));

    const QByteArray reply = process.readAllStandardOutput();
    QDataStream stream(reply);
    stream.setVersion(QDataStream::Qt_6_5);
    QString protocol;
    QByteArray bytes;
    stream >> protocol >> bytes;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() ||
        protocol != QStringLiteral("CsrioXlsx/1") || !bytes.startsWith("PK\003\004"))
        return failure("response", renderer, "incomplete or incompatible renderer response");
    XlsxExportResult result;
    result.bytes.assign(bytes.begin(), bytes.end());
    return result;
}
} // namespace regmap
