#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVariant>

// Opt-in diagnostics only; measures synchronous CPU/I/O phases, not visible frames.
class PhaseTimer {
public:
    PhaseTimer(QObject* owner, const char* property)
        : owner_(qEnvironmentVariableIsSet("REGMAP_PROFILE_PHASES") ? owner : nullptr),
          property_(property)
    {
        if (owner_) timer_.start();
    }
    ~PhaseTimer()
    {
        if (owner_) owner_->setProperty(property_, double(timer_.nsecsElapsed()) / 1e6);
    }
private:
    QObject* owner_;
    const char* property_;
    QElapsedTimer timer_;
};
