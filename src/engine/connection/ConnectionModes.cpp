#include "include/engine/connection/ConnectionManager.h"

#include "include/database/DatabaseManager.h"
#include "include/database/entities/Profile.h"
#include "include/engine/EnginePolicy.h"
#include "include/engine/EngineTr.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/supervisor/CoreSupervisor.h"
#include "include/global/Configs.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/global/Utils.hpp"
#include "include/sys/KillSwitch.hpp"
#include "include/sys/SystemProxy.hpp"

#include <QThread>
#include <QTimer>

#include <utility>

namespace {
    // networksetup and gsettings runs are slow, and profile start/stop call in from their own threads.
    QThread *connectionModesSystemProxyThread() {
        static auto *thread = [] {
            auto *t = new QThread;
            t->start();
            return t;
        }();
        return thread;
    }
}

namespace Engine {
    bool ConnectionManager::ensureTunPrivileges(bool interactive) {
        if (Configs::dataManager->settingsRepo->disable_privilege_req)
        {
            MW_show_log(MainWindowTr::tr("User opted for no privilege req, some features may not work"));
            return true;
        }
        if (Configs::IsAdmin()) return true;
        if (!interactive) return false;
        return policy()->requestCorePrivileges();
    }

    void ConnectionManager::applySystemProxy(bool enable, bool wait) {
        const auto &settings = Configs::dataManager->settingsRepo;
        const auto host = LocalNetwork::InboundConnectHost();
        const auto port = settings->inbound_socks_port;
        const auto format = settings->proxy_scheme;
        runOnThread([=] {
            QString error;
            if (!enable) {
                error = SystemProxy_Clear();
            } else if (Configs::dataManager->settingsRepo->spmode_system_proxy) {
                // Rechecked: a profile start can queue this after the box was unchecked.
                error = SystemProxy_Apply(host, port, format);
            }
            if (!error.isEmpty()) MW_show_log(MainWindowTr::tr("System proxy: %1").arg(error));
        }, connectionModesSystemProxyThread(), wait);
    }

    bool ConnectionManager::setSystemProxy(bool enable, const ModeChange &change) {
        ENGINE_CHECK_MAIN_THREAD();
        const auto &settings = Configs::dataManager->settingsRepo;
        if (enable && settings->disable_mixed_inbound) {
            if (change.interactive) {
                runOnUiThread([=, this] {
                   policy()->systemProxyNeedsMixedInbound();
                });
            }
            emit systemProxyRefused();
            return false;
        }
        settings->spmode_system_proxy = enable;
        bool restarted = false;
        if (const auto running = runningSnapshot()) {
            applySystemProxy(enable);
            if (!enable && settings->reset_proxy_on_disable_sp && change.restart) {
                start(StartRequest{running->id, change.interactive, change.restartSerial});
                restarted = true;
            }
        }

        if (change.save) {
            settings->remember_system_proxy = enable;
            settings->Save();
        }

        emit statusChanged();
        return restarted;
    }

    bool ConnectionManager::setTun(bool enable, const ModeChange &change) {
        ENGINE_CHECK_MAIN_THREAD();
        const auto &settings = Configs::dataManager->settingsRepo;
        if (enable == settings->spmode_vpn) return false;

        if (enable && !Configs::IsAdmin() && !ensureTunPrivileges(change.interactive)) {
            emit statusChanged();
            return false;
        }

        if (change.save) {
            // Written here, after the elevation check, so a failed enable is not remembered.
            settings->remember_tun = enable;
            settings->Save();
        }

        settings->spmode_vpn = enable;
        emit statusChanged();

        if (!change.restart || settings->started_id < 0) return false;
        start(StartRequest{settings->started_id, change.interactive, change.restartSerial});
        return true;
    }

    bool ConnectionManager::chooseRoute(int routeId, bool interactive, quint64 restartSerial) {
        ENGINE_CHECK_MAIN_THREAD();
        const auto &settings = Configs::dataManager->settingsRepo;
        if (settings->current_route_id == routeId) return false;
        settings->current_route_id = routeId;
        settings->Save();
        if (settings->started_id < 0) return false;
        start(StartRequest{settings->started_id, interactive, restartSerial});
        return true;
    }

    void ConnectionManager::attachKillSwitch() {
        connect(Sys::KillSwitch::instance(), &Sys::KillSwitch::stateChanged, this, [this] { onKillSwitchStateChanged(); });
        QTimer::singleShot(0, this, [this] { onKillSwitchStateChanged(); });
    }

    void ConnectionManager::onKillSwitchStateChanged() {
        const auto state = Sys::KillSwitch::instance()->state();
        const bool failed = state == Sys::KillSwitch::State::Failed;
        const bool armed = state == Sys::KillSwitch::State::Armed;
        const bool enteredFailed = failed && !m_killSwitchWasFailed;
        const bool enteredArmed = armed && !m_killSwitchWasArmed;
        m_killSwitchWasFailed = failed;
        m_killSwitchWasArmed = armed;

        emit statusChanged();
        if (!failed) policy()->killSwitchProblemResolved();
        if (enteredFailed) policy()->killSwitchProblem();
        if (enteredArmed && !guardRestartPending() && m_coreSupervisor->runningWithoutGuardIdentity()) {
            StartRequest request{Configs::dataManager->settingsRepo->started_id};
            if (m_coreSupervisor->awaitingCoreStarted() && m_coreSupervisor->startWhenUp() >= 0) {
                // The start waiting for the probing core has not run yet; the guard restart carries it over.
                request = m_coreStartRequest.profileId >= 0 ? std::exchange(m_coreStartRequest, StartRequest{})
                                                            : StartRequest{m_coreSupervisor->startWhenUp()};
            }
            restartCoreForGuard(request);
        }
    }

    void ConnectionManager::disableKillSwitch() {
        Configs::dataManager->settingsRepo->kill_switch = false;
        Configs::dataManager->settingsRepo->Save();
        Sys::KillSwitch::instance()->apply();
    }

    bool ConnectionManager::guardRestartPending() const {
        return m_guardCoreRestart.isValid() && !m_guardCoreRestart.hasExpired(kGuardRestartWindowMs);
    }

    void ConnectionManager::restartCoreForGuard(const StartRequest &request) {
        const bool haveCore = m_coreSupervisor->setStartWhenUp(request.profileId);
        if (!haveCore) {
            emit startFinished(request.serial, request.profileId, StartOutcome::CoreUnavailable, {});
            return;
        }
        deferStartToCore(request);
        if (guardRestartPending()) return;
        m_guardCoreRestart.start();
        MW_show_log(MainWindowTr::tr("[Kill switch] Restarting the core so that its own traffic passes the kill switch..."));
        m_coreSupervisor->restartForGuard();
    }

    void ConnectionManager::noteRestartNeeded(RestartReason reason) {
        ENGINE_CHECK_MAIN_THREAD();
        if (Configs::dataManager->settingsRepo->started_id < 0) return;
        if (!m_restartReasons.contains(reason)) m_restartReasons << reason;
        emit restartReasonsChanged();
    }

    void ConnectionManager::clearRestartNeeded() {
        if (m_restartReasons.isEmpty()) return;
        m_restartReasons.clear();
        emit restartReasonsChanged();
    }

    QList<RestartReason> ConnectionManager::restartReasons() const {
        return m_restartReasons;
    }

    bool ConnectionManager::restartPending() const {
        return !m_restartReasons.isEmpty();
    }
} // namespace Engine
