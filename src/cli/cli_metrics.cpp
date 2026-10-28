#include "cli_metrics.hpp"
namespace regmap::cli {
namespace { thread_local CliMetrics* activeMetrics = nullptr; }
CliMetrics::CliMetrics(bool enabled) : previous_(activeMetrics), enabled_(enabled), start_(std::chrono::steady_clock::now())
{ activeMetrics = enabled ? this : nullptr; }
CliMetrics::~CliMetrics() { activeMetrics = previous_; }
CliMetrics* CliMetrics::current() { return activeMetrics; }
void CliMetrics::record(QString name, double ms, std::uint64_t bytes, std::optional<bool> ok)
{
    if (!enabled_) return;
    if (name.startsWith("generate.")) generatedBytes_ += bytes;
    QJsonObject stage{{"name", name}, {"elapsed_ms", ms}, {"bytes", static_cast<qint64>(bytes)}};
    if (ok) stage.insert("ok", *ok);
    stages_.append(stage);
}
QJsonObject CliMetrics::json() const
{
    return {{"clock", "steady"}, {"total_ms", std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count()},
        {"object_count", static_cast<qint64>(objectCount)}, {"operation_count", static_cast<qint64>(operationCount)},
        {"generated_bytes", static_cast<qint64>(generatedBytes_)}, {"stages", stages_},
        {"boundary", "command dispatch through response encoding; excludes stdout write; stages are sequential subsets of total"}};
}
CliStage::CliStage(QString name) : metrics_(CliMetrics::current()), name_(std::move(name)),
    start_(metrics_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
CliStage::~CliStage() { if(metrics_) metrics_->record(name_, std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count()); }
GenerationObserver generationObserver()
{
    auto* metrics = CliMetrics::current();
    if (!metrics) return {};
    return [metrics](std::string_view stage, double ms, std::uint64_t bytes, bool ok) {
        metrics->record(QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size())), ms, bytes, ok);
    };
}
} // namespace regmap::cli
