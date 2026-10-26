#pragma once

#include <memory>

#include <QString>
#include <QStringList>

namespace Configs {
    class Profile;
}

// Linux-only PPTP split-tunnel egress for Throne outbound dials.
// Uses NetworkManager PPTP (nmcli) with never-default, then host routes via pkexec ip.
namespace Sys::PptpEgress {
    inline constexpr auto kConnectionName = "throne-pptp-egress";

    // Empty when credentials look usable for a connect attempt.
    QString CredentialsProblem();

    bool IsUp();

    // Bring up NM PPTP (or reuse if already connected). Empty on success.
    QString EnsureVpnUp();

    // Resolve profile dial targets and install /32|/128 routes via the PPTP iface. Empty on success.
    QString ApplyProfileRoutes(const std::shared_ptr<Configs::Profile> &profile);

    // Remove routes Throne installed for the last ApplyProfileRoutes; leave the VPN up.
    QString ClearProfileRoutes();

    // ClearProfileRoutes + nmcli connection down for kConnectionName.
    QString TearDown();

    QString CurrentIface();

    QStringList LastRoutedHosts();

    // Connection engine hooks (one place for start/stop branching).
    // Before LoadConfig: bring up PPTP and host routes when enabled. Empty = OK.
    QString PrepareForProfile(const std::shared_ptr<Configs::Profile> &profile, bool tunMode);
    // After profile stop: drop host routes; tear down VPN if PPTP is off and not mid-switch.
    void CleanupAfterProfile(bool switchingProfiles);
    // PPTP server address(es) to keep off GNOME/KDE system proxy.
    QStringList SystemProxyIgnoreHosts();
}
