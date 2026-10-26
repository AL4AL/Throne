#pragma once

#include <memory>

#include <QString>
#include <QStringList>

namespace Configs {
    class Profile;
}

// Linux PPTP / L2TP-IPsec split-tunnel egress for Throne outbound dials.
// NetworkManager (nmcli) with never-default, then host routes via pkexec ip.
// Ubuntu 24.04: network-manager-pptp or network-manager-l2tp (+ strongswan, xl2tpd/go-l2tp).
namespace Sys::PptpEgress {
    inline constexpr auto kConnectionName = "throne-vpn-egress";
    inline constexpr auto kLegacyConnectionName = "throne-pptp-egress";
    inline constexpr auto kStartFailedTitle = "PPTP / L2TP egress";

    struct Diagnosis {
        bool ready = false;
        QString protocol; // pptp | l2tp
        QStringList missingPackages;
        QString summary;
        bool suggestSwitchToL2tp = false;
        bool canInstallPackages = false;
    };

    QString CredentialsProblem();

    Diagnosis Diagnose();

    // pkexec apt-get for Diagnose().missingPackages; caller must have asked the user. Empty on success.
    QString InstallMissingPackages();

    bool IsUp();

    QString EnsureVpnUp();

    QString ApplyProfileRoutes(const std::shared_ptr<Configs::Profile> &profile);

    QString ClearProfileRoutes();

    QString TearDown();

    QString CurrentIface();

    QStringList LastRoutedHosts();

    QString PrepareForProfile(const std::shared_ptr<Configs::Profile> &profile, bool tunMode);

    void CleanupAfterProfile(bool switchingProfiles);

    QStringList SystemProxyIgnoreHosts();
}
