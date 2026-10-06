#pragma once

#include <QJsonObject>
#include <QObject>

#include <memory>

class MainWindow;

namespace SuiteApp {
class Provider;
struct RuntimeStartOptions;
}

namespace regmap::workbench {

class RegMapSuiteIntegration final : public QObject {
public:
    explicit RegMapSuiteIntegration(MainWindow* window,
                                    QObject* parent = nullptr);
    ~RegMapSuiteIntegration() override;

    bool start(QString* failureReason = nullptr);
    bool start(const SuiteApp::RuntimeStartOptions& options,
               QString* failureReason = nullptr);
    [[nodiscard]] bool isRegistered() const;

    static QJsonObject appDescriptor(const QString& version,
                                     const QString& endpoint = {});
    QJsonObject processRequestForTesting(const QJsonObject& request);

private:
    QJsonObject processRequest(const QJsonObject& request);

    MainWindow* window_{nullptr};
    std::unique_ptr<SuiteApp::Provider> provider_;
};

} // namespace regmap::workbench
