#include "include/engine/headless/HeadlessPolicy.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include "include/engine/vpn/VpnEndpoints.h"
#include "include/global/Configs.hpp"
#include "include/sys/KillSwitch.hpp"

namespace Engine {
    void HeadlessPolicy::geoAssetMissingCategory(const QString &contextName, const QString &needed, const QString &whichFile) {
        MW_show_log(QStringLiteral("[Geo assets] The Xray config \"%1\" needs \"%2\", which the installed %3 does not contain; "
                                   "point the GeoIP/GeoSite asset URL at a source that provides it.")
                        .arg(contextName, needed, whichFile));
    }

    bool HeadlessPolicy::confirmGeoAssetDownload(const QString &contextName) {
        MW_show_log(QStringLiteral("[Geo assets] Downloading geoip.dat / geosite.dat for the Xray config \"%1\".").arg(contextName));
        return true;
    }

    void HeadlessPolicy::geoAssetDownloadFinished(const QString &error) {
        if (error.isEmpty()) {
            MW_show_log(QStringLiteral("[Geo assets] Installed; the next start or test uses them."));
        } else {
            MW_show_log(QStringLiteral("[Geo assets] Download failed: %1").arg(error.trimmed()));
        }
    }

    void HeadlessPolicy::killSwitchProblem() {
        const auto *killSwitch = Sys::KillSwitch::instance();
        if (killSwitch->state() != Sys::KillSwitch::State::Failed) return;
        MW_show_log(QStringLiteral("[Kill switch] Not active: %1").arg(killSwitch->failureText()));
    }

    void HeadlessPolicy::killSwitchProblemResolved() {}

    void HeadlessPolicy::extraCoreNotPermitted() {
        MW_show_log(QStringLiteral("[Kill switch] The profile was not started: its extra core could not be allowed through the kill switch."));
    }

    void HeadlessPolicy::startFailed(const StartRequest &request, StartOutcome, const QString &title, const QString &error) {
        MW_show_log(QStringLiteral("Profile %1 failed to start: %2: %3").arg(request.profileId).arg(title, error));
    }

    void HeadlessPolicy::strictRouteUnavailable(const QString &error) {
        MW_show_log(QStringLiteral("Windows could not enable strict routing; disable Strict Route in the Tun settings. Error: %1").arg(error));
    }

    bool HeadlessPolicy::offerCoreReset(const QString &error) {
        MW_show_log(QStringLiteral("Tun did not come up, the core is left running: %1").arg(error));
        return false;
    }

    void HeadlessPolicy::stopFailed(const QString &error) {
        MW_show_log(QStringLiteral("Stop return error: %1").arg(error));
    }

    SlowOperation HeadlessPolicy::slowOperation(SlowOperationKind kind, int delayMs) {
        const auto what = kind == SlowOperationKind::Start ? QStringLiteral("start") : QStringLiteral("stop");
        auto *timer = new QTimer(QCoreApplication::instance());
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, timer, [what, delayMs] {
            MW_show_log(QStringLiteral("The profile %1 has not finished after %2 ms.").arg(what).arg(delayMs));
        });
        timer->start(delayMs);
        return SlowOperation([guard = QPointer<QTimer>(timer)] {
            if (!guard) return;
            guard->stop();
            guard->deleteLater();
        });
    }

    bool HeadlessPolicy::requestCorePrivileges() {
        MW_show_log(QStringLiteral("The core lacks the privileges Tun needs: run Throne as root or as a service, "
                                   "grant CAP_NET_ADMIN, or install a setuid core."));
        return false;
    }

    void HeadlessPolicy::systemProxyNeedsMixedInbound() {
        MW_show_log(QStringLiteral("Cannot set system proxy when mixed inbound is disabled."));
    }

    bool HeadlessPolicy::vpnPromptOpen() const {
        return false;
    }

    void HeadlessPolicy::vpnChallenge(const VpnAuthChallenge &challenge) {
        MW_show_log(QStringLiteral("[VPN] %1 asks for a %2 sign-in that nothing here can answer; it lapses at its deadline.")
                        .arg(Stats::VpnEndpointDisplayName(challenge.endpointTag), challenge.kind));
    }

    void HeadlessPolicy::vpnCredentialsRejected(int profileId, const VpnAuthChallenge &challenge,
                                                const std::function<void(const QString &, const QString &)> &) {
        MW_show_log(QStringLiteral("[VPN] %1 (profile %2) rejected the saved credentials; update them in the profile.")
                        .arg(Stats::VpnEndpointDisplayName(challenge.endpointTag), QString::number(profileId)));
    }

    void HeadlessPolicy::vpnChallengesCleared() {}

    void HeadlessPolicy::settingsNeedTunRestart() {
        MW_show_log(QStringLiteral("Tun settings changed; they take effect when Tun restarts."));
    }

    bool HeadlessPolicy::confirmRestartProgram() {
        MW_show_log(QStringLiteral("A changed setting needs a program restart; it takes effect after the next restart."));
        return false;
    }

    void HeadlessPolicy::testSessionBusy(const QString &text) {
        MW_show_log(text);
    }

    bool HeadlessPolicy::pickInsteadOfStart(int) {
        return false;
    }
} // namespace Engine
