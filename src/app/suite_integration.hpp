#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include <memory>

class MainWindow;

namespace SuiteApp {
class Provider;
class JsonServer;
struct RuntimeStartOptions;
}

namespace regmap::workbench {

class RegMapSuiteIntegration final : public QObject {
public:
    explicit RegMapSuiteIntegration(MainWindow* window,
                                    QObject* parent = nullptr);
    ~RegMapSuiteIntegration() override;

    bool startSession(QString* failureReason = nullptr);
    bool start(QString* failureReason = nullptr);
    bool start(const SuiteApp::RuntimeStartOptions& options,
               QString* failureReason = nullptr);
    [[nodiscard]] bool isRegistered() const;

    static QJsonObject appDescriptor(const QString& version,
                                     const QString& endpoint = {});
    QJsonObject processRequestForTesting(const QJsonObject& request);

private:
    QJsonObject processRequest(const QJsonObject& request);

    QPointer<MainWindow> window_;
    std::unique_ptr<SuiteApp::Provider> provider_;
    std::unique_ptr<SuiteApp::JsonServer> session_;
    QString discoveryEpoch_;
};

} // namespace regmap::workbench
