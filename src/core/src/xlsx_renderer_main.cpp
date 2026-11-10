#include "xlsx_renderer.hpp"
#include "regmap/core/three_way_merge.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLocale>
#include <QScreen>
#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {
int fail(const QString& message)
{
    const auto bytes = message.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    return 1;
}
}

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    static_cast<void>(_setmode(_fileno(stdin), _O_BINARY));
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi);
    QGuiApplication application(argc, argv);
    QLocale::setDefault(QLocale::c());
    const auto arguments = application.arguments();
    if (arguments.size() != 2 || (arguments[1] != "full" && arguments[1] != "consecutive"))
        return fail("Internal XLSX renderer expects full or consecutive on stdin protocol 1.");
    if (QGuiApplication::platformName() != "offscreen" ||
        !application.primaryScreen() || application.primaryScreen()->logicalDotsPerInch() != 96)
        return fail("The XLSX renderer requires the offscreen platform at 96 DPI.");

    const QString fontPath = QDir(application.applicationDirPath()).filePath("resources/xlsx/NotoSansSC.ttf");
    QFile fontFile(fontPath);
    if (!fontFile.open(QIODevice::ReadOnly)) return fail("Cannot read XLSX font: " + fontPath);
    const auto fontBytes = fontFile.readAll();
    if (QCryptographicHash::hash(fontBytes, QCryptographicHash::Sha256).toHex() != REGMAP_XLSX_FONT_SHA256)
        return fail("XLSX font checksum mismatch: " + fontPath);
    const int fontId = QFontDatabase::addApplicationFontFromData(fontBytes);
    if (fontId < 0 || !QFontDatabase::applicationFontFamilies(fontId).contains("Noto Sans SC"))
        return fail("Cannot load the bundled Noto Sans SC font: " + fontPath);
    QFont font(QStringLiteral("Noto Sans SC"), 10);
    font.setStyleStrategy(QFont::NoFontMerging);
    application.setFont(font);

    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return fail("Cannot read the XLSX input snapshot.");
    const auto data = input.readAll();
    const auto loaded = regmap::parseWorkspaceState(
        std::string_view(data.constData(), static_cast<size_t>(data.size())));
    if (!loaded.workspace || !loaded.diagnostics.empty()) {
        QString message = "Invalid XLSX input snapshot.";
        for (const auto& diagnostic : loaded.diagnostics)
            message += "\n" + QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
        return fail(message);
    }
    const auto result = regmap::detail::renderReadOnlyWorkbook(*loaded.workspace, arguments[1] == "consecutive");
    if (result.bytes.empty() || !result.diagnostics.empty()) {
        QString message = "Cannot render the XLSX snapshot.";
        for (const auto& diagnostic : result.diagnostics)
            message += "\n" + QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
        return fail(message);
    }
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return fail("Cannot write the XLSX response.");
    QDataStream stream(&output);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << QStringLiteral("CsrioXlsx/1") << QByteArray(
        reinterpret_cast<const char*>(result.bytes.data()), static_cast<qsizetype>(result.bytes.size()));
    return stream.status() == QDataStream::Ok && output.flush() ? 0 : fail("Incomplete XLSX response.");
}
