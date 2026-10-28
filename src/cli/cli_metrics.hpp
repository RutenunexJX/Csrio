#pragma once
#include "regmap/core/generation.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <chrono>
#include <utility>
#include <optional>
namespace regmap::cli {
class CliMetrics {
public:
    explicit CliMetrics(bool enabled);
    ~CliMetrics();
    CliMetrics(const CliMetrics&) = delete;
    CliMetrics& operator=(const CliMetrics&) = delete;
    static CliMetrics* current();
    void record(QString name, double ms, std::uint64_t bytes = 0, std::optional<bool> ok = std::nullopt);
    QJsonObject json() const;
    std::uint64_t objectCount{0}, operationCount{0};
private:
    CliMetrics* previous_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
    QJsonArray stages_;
    std::uint64_t generatedBytes_{0};
};
class CliStage {
public:
    explicit CliStage(QString name);
    ~CliStage();
private:
    CliMetrics* metrics_;
    QString name_;
    std::chrono::steady_clock::time_point start_;
};
template<typename F> auto timed(QString name, F&& call) {
    const CliStage stage(std::move(name));
    return std::forward<F>(call)();
}
[[nodiscard]] GenerationObserver generationObserver();
} // namespace regmap::cli
