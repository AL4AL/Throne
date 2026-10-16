#include "include/sys/Process.hpp"
#include "include/global/Configs.hpp"

namespace Configs_sys {
    CoreProcess::~CoreProcess() {
    }

    void CoreProcess::Kill() {
        kill();
        waitForFinished();
    }

    CoreProcess::CoreProcess(const QString &core_path, const QString &socketName, bool debugMode)
        : m_socketName(socketName), m_debugMode(debugMode) {
        program = core_path;
    }

    void CoreProcess::Start() {
        if (started) return;
        started = true;
        ++generation;
        failed_to_start = false;

        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("THRONE_CORE_SOCKET", m_socketName);
        // Turns an unrecovered Go panic into a real abort, so all goroutine stacks are dumped and WER captures the core too.
        env.insert("GOTRACEBACK", "crash");
        if (m_debugMode) env.insert("THRONE_CORE_DEBUG", "1");
        // Points Xray's asset loader at our writable config dir, so a geoip.dat/geosite.dat downloaded later is found with no core restart.
        env.insert("XRAY_LOCATION_ASSET", Configs::GetBasePath());
        guard_identity = Configs::dataManager->settingsRepo->kill_switch;
        if (guard_identity) env.insert("THRONE_GUARD", "1");
        setProcessEnvironment(env);
        start(program, {});
    }

    void CoreProcess::Restart() {
        restarting = true;
        kill();
        waitForFinished(500);
        started = false;
        Start();
        restarting = false;
    }

} // namespace Configs_sys
