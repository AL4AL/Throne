#pragma once

#include "include/engine/EngineControl.h"

class MainWindow;

class MainWindowEngineControl final : public Engine::EngineControl {
    Q_OBJECT

public:
    explicit MainWindowEngineControl(MainWindow *mw);

    Engine::ConnectionState connectionState() const override;

    std::shared_ptr<Configs::Profile> runningSnapshot() const override;

    int resolveLastProfile() override;

    quint64 nextStartSerial() override;

    void start(const Engine::StartRequest &request) override;

    void stop(const Engine::StopOptions &options) override;

    bool setSystemProxy(bool enable, const Engine::ModeChange &change) override;

    bool setTun(bool enable, const Engine::ModeChange &change) override;

    bool chooseRoute(int routeId, bool interactive, quint64 serial) override;

    int clearTestResults(const QList<int> &profileIds) override;

    std::optional<Engine::LiveRates> freshLiveRates(qint64 maxAgeMs) const override;

    bool testsRunning() override;

    bool testsStopRequested() const override;

    bool runTests(Engine::TestKind kind, const QList<int> &profileIds, const std::function<void()> &onFinished) override;

    void queueUrlTests(const QList<int> &profileIds, const std::function<void()> &onFinished) override;

    void stopTests() override;

    void noteRestartNeeded(Engine::RestartReason reason) override;

    bool restartPending() const override;

    bool requestExit(Engine::ExitReason reason) override;

private:
    MainWindow *mw_;
    bool exitQueued_ = false;
};
