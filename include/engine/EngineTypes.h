#pragma once

#include <QtGlobal>

namespace Engine {
    enum class ConnectionState {
        Idle,
        Connecting,
        Running,
        Stopping,
    };

    enum class StartOutcome {
        Started,
        Exiting,
        NotFound,
        GroupUnavailable,
        KillSwitchInactive,
        Superseded,
        CoreUnavailable,
        BuildFailed,
        Busy,
        ExtraCoreBlocked,
        GeoAssetsMissing,
        StrictRouteUnavailable,
        TunFailed,
        StartFailed,
    };

    struct StartRequest {
        int profileId = -1;
        // Unattended callers get every failure through start_finished and the log, never a dialog.
        bool interactive = true;
        quint64 serial = 0;
    };

    struct StopOptions {
        bool crash = false;
        bool block = false;
        bool manual = false;
        bool interactive = true;
    };

    struct ModeChange {
        bool save = true;
        // Unattended callers get no dialog or elevation prompt, and their restart is an unattended StartRequest carrying restartSerial.
        bool interactive = true;
        bool restart = true;
        quint64 restartSerial = 0;
    };

    // Why the running profile should restart to apply a change; the view translates it.
    enum class RestartReason {
        Settings,
        Routing,
        Profile,
        Group,
    };

    enum class ExitReason {
        None,
        RunUpdater,
        Restart,
        RestartWithTun,
        RestartElevated,
    };

    // Bytes per second.
    struct LiveRates {
        qint64 proxyUp = 0;
        qint64 proxyDown = 0;
        qint64 directUp = 0;
        qint64 directDown = 0;
    };

    enum class TestKind {
        Latency,
        Ip,
        Speed,
    };
} // namespace Engine
