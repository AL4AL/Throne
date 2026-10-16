#pragma once

#include <atomic>
#include <memory>
#include <QProcess>

namespace Configs_sys {
    // The ThroneCore process; Engine::CoreSupervisor connects its handlers and owns the restart policy.
    class CoreProcess : public QProcess
    {
    public:
        QString tag;
        QString program;
        QStringList arguments;

        ~CoreProcess();

        // start & kill is one time

        void Start();

        void Kill();

        CoreProcess(const QString &core_path, const QString &socketName, bool debugMode);

        void Restart();

        // A core adopts the kill switch group (Linux/macOS) only at startup; this records whether the running one was asked to.
        std::atomic<bool> guard_identity = false;

        // Bumped by each Start(), so an exit or connection can be matched to the attempt that produced it.
        std::atomic<quint64> generation = 0;

        // DS_cores only; failed_to_start is reset by each Start().
        bool failed_to_start = false;
        bool restarting = false;

    private:
        QString m_socketName;
        bool m_debugMode = false;

    protected:
        bool started = false;
    };
} // namespace Configs_sys
