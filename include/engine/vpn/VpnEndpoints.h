#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace libcore {
    struct VPNEndpointStatus;
}

namespace Stats {
    // Qt-side copy of libcore::VPNEndpointStatus, built off the UI thread.
    struct VpnEndpointView {
        QString tag;
        QString displayName;
        QString state;
        QString error;
        QString server;
        QString network;
        QString cipher;
        QStringList ipv4;
        QStringList ipv6;
        QStringList dns;
        QStringList routes;
        QStringList excludedRoutes;
        QStringList domains;
        int mtu = 0;
        qint64 connectedSince = 0;
        bool connected = false;
    };

    VpnEndpointView MakeVpnEndpointView(const libcore::VPNEndpointStatus &status);

    QString VpnStateText(const QString &state);

    QString HumanizeDuration(qint64 seconds);

    // Tag -> profile id from the last build; a live status only carries its config tag.
    void SetVpnEndpointProfiles(const QMap<QString, int> &tagToProfileID);

    QString VpnEndpointDisplayName(const QString &tag);

    int VpnEndpointProfileID(const QString &tag);
} // namespace Stats
