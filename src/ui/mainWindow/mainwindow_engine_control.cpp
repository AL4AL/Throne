#include "include/ui/mainWindow/MainWindowEngineControl.h"

#include "include/ui/mainwindow.h"
#include "include/engine/services/StatsService.h"
#include "include/engine/tests/TestRunner.h"

MainWindowEngineControl::MainWindowEngineControl(MainWindow *mw) : EngineControl(mw), mw_(mw) {
    connect(mw, &MainWindow::start_finished, this, &Engine::EngineControl::startFinished, Qt::DirectConnection);
    connect(mw, &MainWindow::stop_finished, this, &Engine::EngineControl::stopFinished, Qt::DirectConnection);
    connect(mw, &MainWindow::connection_state_changed, this, &Engine::EngineControl::connectionStateChanged, Qt::DirectConnection);
}

Engine::ConnectionState MainWindowEngineControl::connectionState() const {
    return mw_->connection_state();
}

std::shared_ptr<Configs::Profile> MainWindowEngineControl::runningSnapshot() const {
    return mw_->runningSnapshot();
}

int MainWindowEngineControl::resolveLastProfile() {
    return mw_->resolve_last_profile();
}

quint64 MainWindowEngineControl::nextStartSerial() {
    return mw_->next_start_serial();
}

void MainWindowEngineControl::start(const Engine::StartRequest &request) {
    mw_->profile_start(request);
}

void MainWindowEngineControl::stop(const Engine::StopOptions &options) {
    mw_->profile_stop(options.crash, options.block, options.manual, options.interactive);
}

bool MainWindowEngineControl::setSystemProxy(bool enable, const Engine::ModeChange &change) {
    return mw_->set_spmode_system_proxy(enable, change);
}

bool MainWindowEngineControl::setTun(bool enable, const Engine::ModeChange &change) {
    return mw_->set_spmode_vpn(enable, change);
}

bool MainWindowEngineControl::chooseRoute(int routeId, bool interactive, quint64 serial) {
    return mw_->choose_route(routeId, interactive, serial);
}

int MainWindowEngineControl::clearTestResults(const QList<int> &profileIds) {
    return mw_->clear_test_results(profileIds);
}

std::optional<Engine::LiveRates> MainWindowEngineControl::freshLiveRates(qint64 maxAgeMs) const {
    if (mw_->runningSnapshot() == nullptr) return std::nullopt;
    return mw_->m_stats->freshLiveRates(maxAgeMs);
}

bool MainWindowEngineControl::testsRunning() {
    return mw_->testRunner->isRunning();
}

bool MainWindowEngineControl::testsStopRequested() const {
    return mw_->testRunner->stopRequested();
}

bool MainWindowEngineControl::runTests(Engine::TestKind kind, const QList<int> &profileIds, const std::function<void()> &onFinished) {
    switch (kind) {
        case Engine::TestKind::Latency: return mw_->testRunner->runUrlTests(profileIds, onFinished, false);
        case Engine::TestKind::Ip: return mw_->testRunner->runIpTests(profileIds, onFinished, false);
        case Engine::TestKind::Speed: break;
    }
    return mw_->testRunner->runSpeedTests(profileIds, false, onFinished, false);
}

void MainWindowEngineControl::queueUrlTests(const QList<int> &profileIds, const std::function<void()> &onFinished) {
    mw_->testRunner->queueUrlTests(profileIds, onFinished, false);
}

void MainWindowEngineControl::stopTests() {
    mw_->testRunner->stop();
}

void MainWindowEngineControl::noteRestartNeeded(Engine::RestartReason reason) {
    QString text;
    switch (reason) {
        case Engine::RestartReason::Settings: text = MainWindow::tr("Settings"); break;
        case Engine::RestartReason::Routing: text = MainWindow::tr("Routing"); break;
        case Engine::RestartReason::Profile: text = MainWindow::tr("Profile"); break;
        case Engine::RestartReason::Group: text = MainWindow::tr("Group"); break;
    }
    mw_->noteRestartNeeded(text);
}

bool MainWindowEngineControl::restartPending() const {
    return !mw_->pendingRestartReasons().isEmpty();
}

bool MainWindowEngineControl::requestExit(Engine::ExitReason reason) {
    if (reason != Engine::ExitReason::None && reason != Engine::ExitReason::Restart) return false;
    if (exitQueued_ || mw_->m_exitInProgress || Configs::dataManager->settingsRepo->prepare_exit) return false;
    exitQueued_ = true;
    // Queued so the caller replies before the teardown shuts the API server down.
    QMetaObject::invokeMethod(mw_, [this, reason] {
        mw_->request_unattended_exit(reason);
        // Turned away by a window exit waiting on UAC: once that one is declined, a new request may run.
        if (!Configs::dataManager->settingsRepo->prepare_exit) exitQueued_ = false;
    }, Qt::QueuedConnection);
    return true;
}
