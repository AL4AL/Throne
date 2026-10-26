#include "include/sys/linux/PptpEgress.h"

#include "include/configs/common/Outbound.h"
#include "include/configs/generate.h"
#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/global/Validation.hpp"
#include "include/sys/linux/LinuxCap.h"

#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QRecursiveMutex>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace {
    QRecursiveMutex egressMutex;
    QStringList routedHosts;
    QString routedIface;

    QString NmcliPath() { return QStandardPaths::findExecutable(QStringLiteral("nmcli")); }

    QString IpPath() { return Linux_FindCapProgsExec(QStringLiteral("ip")); }

    QString RunNmcli(const QStringList &args, int timeoutMs = 60000) {
        const auto nmcli = NmcliPath();
        if (nmcli.isEmpty()) {
            return QObject::tr("nmcli was not found; install NetworkManager (network-manager).");
        }
        QProcess p;
        p.setProgram(nmcli);
        p.setArguments(args);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start();
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            p.waitForFinished(3000);
            return QObject::tr("nmcli timed out: %1").arg(args.join(QLatin1Char(' ')));
        }
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            auto out = QString::fromLocal8Bit(p.readAll()).trimmed();
            if (out.isEmpty()) out = QObject::tr("exit code %1").arg(p.exitCode());
            return out;
        }
        return {};
    }

    QString NmcliStdout(const QStringList &args, int timeoutMs = 15000) {
        const auto nmcli = NmcliPath();
        if (nmcli.isEmpty()) return {};
        QProcess p;
        p.start(nmcli, args);
        if (!p.waitForFinished(timeoutMs)) return {};
        return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    }

    bool HasConnectionNamed(const QString &name) {
        return NmcliStdout({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME"), QStringLiteral("connection"),
                            QStringLiteral("show")})
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts)
            .contains(name);
    }

    bool ConnectionIsActive(const QString &name) {
        return NmcliStdout({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME"), QStringLiteral("connection"),
                            QStringLiteral("show"), QStringLiteral("--active")})
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts)
            .contains(name);
    }

    QString ActivePppDevice() {
        const auto nmcli = NmcliPath();
        if (!nmcli.isEmpty()) {
            QProcess p;
            p.start(nmcli, {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DEVICE,TYPE,CONNECTION"),
                            QStringLiteral("device"), QStringLiteral("status")});
            if (p.waitForFinished(10000)) {
                for (const auto &line : QString::fromLocal8Bit(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                    const auto parts = line.split(QLatin1Char(':'));
                    if (parts.size() < 3) continue;
                    if (parts[2] != QLatin1String(Sys::PptpEgress::kConnectionName) &&
                        parts[2] != QLatin1String(Sys::PptpEgress::kLegacyConnectionName)) {
                        continue;
                    }
                    if (parts[0].startsWith(QStringLiteral("ppp"))) return parts[0];
                }
            }
        }
        QProcess ip;
        ip.start(IpPath(), {QStringLiteral("-br"), QStringLiteral("link"), QStringLiteral("show"), QStringLiteral("type"),
                            QStringLiteral("ppp")});
        if (!ip.waitForFinished(5000)) return {};
        for (const auto &line : QString::fromLocal8Bit(ip.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const auto tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (!tok.isEmpty() && tok[0].startsWith(QStringLiteral("ppp"))) return tok[0];
        }
        return {};
    }

    QString PrivilegedIpScript(const QString &script) {
        if (!Linux_HavePkexec()) {
            return QObject::tr("pkexec was not found; cannot change routes or MTU.");
        }
        QProcess p;
        p.setProgram(QStringLiteral("pkexec"));
        p.setArguments({QStringLiteral("/bin/bash"), QStringLiteral("-c"), script});
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start();
        if (!p.waitForFinished(120000)) {
            p.kill();
            p.waitForFinished(3000);
            return QObject::tr("pkexec timed out while applying VPN routes/MTU.");
        }
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            auto out = QString::fromLocal8Bit(p.readAll()).trimmed();
            if (out.isEmpty()) out = QObject::tr("exit code %1").arg(p.exitCode());
            return out;
        }
        return {};
    }

    QString DialHostForProfile(const std::shared_ptr<Configs::Profile> &profile) {
        if (profile == nullptr || profile->outbound == nullptr) return {};
        const auto source = Configs::EffectiveEndpointSource(*profile);
        const auto resolution = Configs::ResolveEndpointSource(source);
        if (!resolution.address.isEmpty()) return resolution.address.trimmed();
        return profile->outbound->GetAddress().trimmed();
    }

    QStringList ResolveHostToIps(const QString &host) {
        QStringList ips;
        if (host.isEmpty()) return ips;
        if (Validation::Ip(host).isEmpty()) {
            ips << host;
            return ips;
        }
        for (const auto &addr : QHostInfo::fromName(host).addresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol || addr.protocol() == QAbstractSocket::IPv6Protocol) {
                const auto s = addr.toString();
                if (!ips.contains(s)) ips << s;
            }
        }
        return ips;
    }

    QString HostRouteSuffix(const QString &ip) {
        return QHostAddress(ip).protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("/128") : QStringLiteral("/32");
    }

    void ClearRoutesLocked() {
        if (routedHosts.isEmpty()) return;
        const auto ip = IpPath();
        QString script;
        for (const auto &addr : routedHosts) {
            script += QStringLiteral("%1 route del %2%3 dev %4 2>/dev/null || true\n")
                          .arg(ip, addr, HostRouteSuffix(addr),
                               routedIface.isEmpty() ? QStringLiteral("ppp0") : routedIface);
        }
        routedHosts.clear();
        routedIface.clear();
        PrivilegedIpScript(script);
    }

    bool PackageInstalled(const QString &name) {
        auto dpkg = QStandardPaths::findExecutable(QStringLiteral("dpkg-query"));
        if (dpkg.isEmpty()) dpkg = QStringLiteral("/usr/bin/dpkg-query");
        QProcess p;
        p.start(dpkg, {QStringLiteral("-W"), QStringLiteral("-f=${Status}"), name});
        if (!p.waitForFinished(5000)) return false;
        return QString::fromLocal8Bit(p.readAllStandardOutput()).contains(QLatin1String("install ok installed"));
    }

    bool VpnPluginPresent(const QString &shortName) {
        const QString file = QStringLiteral("nm-%1-service.name").arg(shortName);
        return QFile::exists(QStringLiteral("/usr/lib/NetworkManager/VPN/") + file) ||
               QFile::exists(QStringLiteral("/usr/lib/x86_64-linux-gnu/NetworkManager/VPN/") + file) ||
               QFile::exists(QStringLiteral("/usr/lib/aarch64-linux-gnu/NetworkManager/VPN/") + file);
    }

    QString SelectedProtocol() {
        return Configs::dataManager->settingsRepo->egress_protocol.trimmed().toLower() == QLatin1String("l2tp")
                   ? QStringLiteral("l2tp")
                   : QStringLiteral("pptp");
    }

    // nmcli vpn.data / vpn.secrets use comma-separated key=value; reject values that would break the dict.
    QString NmcliDictProblem(const QString &label, const QString &value) {
        if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('='))) {
            return QObject::tr("%1 cannot contain comma or equals (NetworkManager vpn.secrets format).").arg(label);
        }
        return {};
    }

    QString RecentNmJournal(const QString &connectionName) {
        const auto uuid = NmcliStdout({QStringLiteral("-t"), QStringLiteral("-g"), QStringLiteral("connection.uuid"),
                                       QStringLiteral("connection"), QStringLiteral("show"), connectionName});
        QProcess p;
        if (!uuid.isEmpty()) {
            p.start(QStringLiteral("journalctl"),
                    {QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("12"), QStringLiteral("--no-pager"),
                     QStringLiteral("NM_CONNECTION=%1").arg(uuid)});
        } else {
            p.start(QStringLiteral("journalctl"),
                    {QStringLiteral("-b"), QStringLiteral("-u"), QStringLiteral("NetworkManager"), QStringLiteral("-n"),
                     QStringLiteral("12"), QStringLiteral("--no-pager")});
        }
        if (!p.waitForFinished(8000)) return {};
        return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    }

    void DeleteConnectionIfPresent(const QString &name) {
        if (!HasConnectionNamed(name)) return;
        if (ConnectionIsActive(name)) RunNmcli({QStringLiteral("connection"), QStringLiteral("down"), name}, 30000);
        RunNmcli({QStringLiteral("connection"), QStringLiteral("delete"), name}, 30000);
    }
}

namespace Sys::PptpEgress {
    QString CredentialsProblem() {
        const auto &s = Configs::dataManager->settingsRepo;
        if (s->pptp_gateway.trimmed().isEmpty()) return QObject::tr("VPN server is not configured.");
        if (auto p = Validation::Host(s->pptp_gateway.trimmed()); !p.isEmpty()) return p;
        if (s->pptp_username.trimmed().isEmpty()) return QObject::tr("VPN username is empty.");
        if (s->pptp_password.isEmpty()) return QObject::tr("VPN password is empty.");
        if (SelectedProtocol() == QLatin1String("l2tp") && s->egress_ipsec_psk.isEmpty()) {
            return QObject::tr("L2TP/IPsec needs a pre-shared key. Enter it in PPTP / L2TP Egress settings.");
        }
        if (s->pptp_mtu < 576 || s->pptp_mtu > 1500) return QObject::tr("VPN MTU must be between 576 and 1500.");
        if (auto e = NmcliDictProblem(QObject::tr("Server"), s->pptp_gateway.trimmed()); !e.isEmpty()) return e;
        if (auto e = NmcliDictProblem(QObject::tr("Username"), s->pptp_username.trimmed()); !e.isEmpty()) return e;
        if (auto e = NmcliDictProblem(QObject::tr("Password"), s->pptp_password); !e.isEmpty()) return e;
        if (auto e = NmcliDictProblem(QObject::tr("Pre-shared key"), s->egress_ipsec_psk); !e.isEmpty()) return e;
        return {};
    }

    Diagnosis Diagnose() {
        Diagnosis d;
        d.protocol = SelectedProtocol();
        QStringList lines;
        const bool l2tp = d.protocol == QLatin1String("l2tp");

        if (NmcliPath().isEmpty()) {
            d.summary = QObject::tr("nmcli was not found. Install network-manager (Ubuntu Desktop default).");
            d.missingPackages = {QStringLiteral("network-manager")};
            d.canInstallPackages = QFile::exists(QStringLiteral("/usr/bin/apt-get")) && Linux_HavePkexec();
            return d;
        }
        lines << QObject::tr("NetworkManager (nmcli): OK");

        // Ubuntu 24.04 noble packages from the official archive.
        if (l2tp) {
            if (!PackageInstalled(QStringLiteral("network-manager-l2tp")) || !VpnPluginPresent(QStringLiteral("l2tp"))) {
                d.missingPackages << QStringLiteral("network-manager-l2tp");
            }
            if (!PackageInstalled(QStringLiteral("strongswan")) && !PackageInstalled(QStringLiteral("libreswan"))) {
                d.missingPackages << QStringLiteral("strongswan");
            }
            if (!PackageInstalled(QStringLiteral("go-l2tp")) && !PackageInstalled(QStringLiteral("xl2tpd"))) {
                d.missingPackages << QStringLiteral("xl2tpd");
            }
        } else {
            if (!PackageInstalled(QStringLiteral("network-manager-pptp")) || !VpnPluginPresent(QStringLiteral("pptp"))) {
                d.missingPackages << QStringLiteral("network-manager-pptp");
            }
            if (!PackageInstalled(QStringLiteral("pptp-linux"))) d.missingPackages << QStringLiteral("pptp-linux");
        }
        d.missingPackages.removeDuplicates();

        const bool pluginOk = VpnPluginPresent(l2tp ? QStringLiteral("l2tp") : QStringLiteral("pptp"));
        lines << (pluginOk ? QObject::tr("VPN plugin (%1): OK").arg(d.protocol)
                           : QObject::tr("VPN plugin (%1): missing").arg(d.protocol));
        if (!d.missingPackages.isEmpty()) {
            lines << QObject::tr("Missing packages: %1").arg(d.missingPackages.join(QStringLiteral(", ")));
        } else {
            lines << QObject::tr("Required packages: installed");
        }

        if (!Linux_HavePkexec()) {
            lines << QObject::tr("pkexec is missing (needed for routes, MTU, and package installs).");
            if (!PackageInstalled(QStringLiteral("pkexec")) && !PackageInstalled(QStringLiteral("policykit-1"))) {
                d.missingPackages << QStringLiteral("pkexec");
            }
        }

        if (const auto cred = CredentialsProblem(); !cred.isEmpty()) lines << cred;

        if (!l2tp && !Configs::dataManager->settingsRepo->egress_ipsec_psk.trimmed().isEmpty()) {
            d.suggestSwitchToL2tp = true;
            lines << QObject::tr("A pre-shared key is set, but PPTP ignores it — choose L2TP/IPsec.");
        }

        d.canInstallPackages = !d.missingPackages.isEmpty() && QFile::exists(QStringLiteral("/usr/bin/apt-get")) && Linux_HavePkexec();
        d.ready = d.missingPackages.isEmpty() && pluginOk && CredentialsProblem().isEmpty();
        d.summary = lines.join(QLatin1Char('\n'));
        return d;
    }

    QString InstallMissingPackages() {
        auto diag = Diagnose();
        if (diag.missingPackages.isEmpty()) return {};
        if (!Linux_HavePkexec()) return QObject::tr("pkexec was not found; cannot install packages.");
        if (!QFile::exists(QStringLiteral("/usr/bin/apt-get"))) return QObject::tr("apt-get was not found.");

        const QString pkgs = diag.missingPackages.join(QLatin1Char(' '));
        const QString script = QStringLiteral(
                                   "export DEBIAN_FRONTEND=noninteractive; "
                                   "/usr/bin/apt-get update -qq || true; "
                                   "/usr/bin/apt-get install -y %1; "
                                   "systemctl reload NetworkManager 2>/dev/null || systemctl try-restart NetworkManager 2>/dev/null || true")
                                   .arg(pkgs);

        QProcess p;
        p.setProgram(QStringLiteral("pkexec"));
        p.setArguments({QStringLiteral("/bin/bash"), QStringLiteral("-c"), script});
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start();
        if (!p.waitForFinished(600000)) {
            p.kill();
            p.waitForFinished(3000);
            return QObject::tr("Package install timed out.");
        }
        const auto out = QString::fromLocal8Bit(p.readAll()).trimmed();
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            return QObject::tr("Could not install %1:\n%2").arg(pkgs, out.isEmpty() ? QObject::tr("exit code %1").arg(p.exitCode()) : out);
        }

        QThread::msleep(1500);
        diag = Diagnose();
        if (!diag.missingPackages.isEmpty()) {
            return QObject::tr("Packages installed, but something is still missing. Log out/in or reboot, then try again.\n%1")
                .arg(diag.summary);
        }
        return {};
    }

    bool IsUp() {
        return ConnectionIsActive(QString::fromUtf8(kConnectionName)) || ConnectionIsActive(QString::fromUtf8(kLegacyConnectionName));
    }

    QString EnsureVpnUp() {
        QMutexLocker lock(&egressMutex);
        if (const auto problem = CredentialsProblem(); !problem.isEmpty()) return problem;
        if (NmcliPath().isEmpty()) return QObject::tr("nmcli was not found; install NetworkManager.");

        const auto diag = Diagnose();
        if (!diag.missingPackages.isEmpty()) {
            return QObject::tr("VPN packages are missing. Open PPTP / L2TP Egress → Check & fix system, or install: %1")
                .arg(diag.missingPackages.join(QLatin1Char(' ')));
        }

        const auto &s = Configs::dataManager->settingsRepo;
        const auto protocol = SelectedProtocol();
        const bool l2tp = protocol == QLatin1String("l2tp");
        const QString name = QString::fromUtf8(kConnectionName);
        const QString gateway = s->pptp_gateway.trimmed();
        const QString user = s->pptp_username.trimmed();
        const QString password = s->pptp_password;
        const QString psk = s->egress_ipsec_psk;
        const int mtu = s->pptp_mtu;

        // Drop the legacy profile name and any wrong vpn-type under the current name.
        DeleteConnectionIfPresent(QString::fromUtf8(kLegacyConnectionName));
        if (HasConnectionNamed(name)) {
            const auto service = NmcliStdout({QStringLiteral("-t"), QStringLiteral("-g"), QStringLiteral("vpn.service-type"),
                                              QStringLiteral("connection"), QStringLiteral("show"), name});
            const QString want = l2tp ? QStringLiteral("org.freedesktop.NetworkManager.l2tp")
                                      : QStringLiteral("org.freedesktop.NetworkManager.pptp");
            if (!service.isEmpty() && service != want) DeleteConnectionIfPresent(name);
        }

        QString vpnData;
        QString vpnSecrets = QStringLiteral("password=%1").arg(password);
        QString vpnType;
        if (l2tp) {
            vpnType = QStringLiteral("l2tp");
            vpnData = QStringLiteral("gateway=%1,user=%2,password-flags=0,ipsec-enabled=yes,machine-auth-type=psk").arg(gateway, user);
            vpnSecrets += QStringLiteral(",ipsec-psk=%1").arg(psk);
        } else {
            vpnType = QStringLiteral("pptp");
            vpnData = QStringLiteral("gateway=%1,user=%2,password-flags=0").arg(gateway, user);
        }

        if (!HasConnectionNamed(name)) {
            if (auto err = RunNmcli({QStringLiteral("connection"), QStringLiteral("add"), QStringLiteral("type"), QStringLiteral("vpn"),
                                    QStringLiteral("con-name"), name, QStringLiteral("ifname"), QStringLiteral("*"),
                                    QStringLiteral("vpn-type"), vpnType, QStringLiteral("vpn.data"), vpnData, QStringLiteral("vpn.secrets"),
                                    vpnSecrets, QStringLiteral("ipv4.never-default"), QStringLiteral("yes"),
                                    QStringLiteral("ipv6.never-default"), QStringLiteral("yes"), QStringLiteral("connection.autoconnect"),
                                    QStringLiteral("no")});
                !err.isEmpty()) {
                return QObject::tr("Could not create the %1 connection (%2).").arg(protocol, err);
            }
        } else if (auto err = RunNmcli({QStringLiteral("connection"), QStringLiteral("modify"), name, QStringLiteral("vpn.data"), vpnData,
                                       QStringLiteral("vpn.secrets"), vpnSecrets, QStringLiteral("ipv4.never-default"), QStringLiteral("yes"),
                                       QStringLiteral("ipv6.never-default"), QStringLiteral("yes"), QStringLiteral("connection.autoconnect"),
                                       QStringLiteral("no")});
                   !err.isEmpty()) {
            return QObject::tr("Could not update the %1 connection (%2).").arg(protocol, err);
        }

        if (!ConnectionIsActive(name)) {
            if (auto err = RunNmcli({QStringLiteral("connection"), QStringLiteral("up"), name}, 90000); !err.isEmpty()) {
                QString msg = QObject::tr("Could not bring up %1 (%2).").arg(protocol.toUpper(), err);
                if (!l2tp) {
                    msg += QLatin1Char('\n') +
                           QObject::tr("Hint: providers labeled “PPTP / L2TP” with a pre-shared key usually need L2TP/IPsec, not PPTP.");
                } else {
                    msg += QLatin1Char('\n') + QObject::tr("Hint: check server, username, password, and IPsec pre-shared key.");
                }
                if (const auto journal = RecentNmJournal(name); !journal.isEmpty()) {
                    msg += QLatin1Char('\n') + QObject::tr("NetworkManager log:") + QLatin1Char('\n') + journal;
                }
                return msg;
            }
        }

        QString iface;
        for (int attempt = 0; attempt < 30; ++attempt) {
            iface = ActivePppDevice();
            if (!iface.isEmpty()) break;
            QThread::msleep(500);
        }
        if (iface.isEmpty()) {
            QString msg = QObject::tr("VPN connected but no ppp interface appeared.");
            if (const auto journal = RecentNmJournal(name); !journal.isEmpty()) msg += QLatin1Char('\n') + journal;
            return msg;
        }

        if (auto err = PrivilegedIpScript(QStringLiteral("%1 link set dev %2 mtu %3").arg(IpPath(), iface, QString::number(mtu)));
            !err.isEmpty()) {
            return QObject::tr("Could not set VPN MTU: %1").arg(err);
        }
        return {};
    }

    QString ApplyProfileRoutes(const std::shared_ptr<Configs::Profile> &profile) {
        if (auto err = EnsureVpnUp(); !err.isEmpty()) return err;

        QMutexLocker lock(&egressMutex);
        ClearRoutesLocked();

        const auto host = DialHostForProfile(profile);
        if (host.isEmpty()) return QObject::tr("Profile has no server address to route via the VPN egress.");

        const auto ips = ResolveHostToIps(host);
        if (ips.isEmpty()) return QObject::tr("Could not resolve profile server %1 for VPN egress routing.").arg(host);

        const auto iface = ActivePppDevice();
        if (iface.isEmpty()) return QObject::tr("No VPN ppp interface for host routes.");

        const auto ip = IpPath();
        QString script;
        for (const auto &addr : ips) {
            script += QStringLiteral("%1 route replace %2%3 dev %4\n").arg(ip, addr, HostRouteSuffix(addr), iface);
        }
        if (auto err = PrivilegedIpScript(script); !err.isEmpty()) {
            return QObject::tr("Could not install VPN host routes: %1").arg(err);
        }
        routedHosts = ips;
        routedIface = iface;
        return {};
    }

    QString ClearProfileRoutes() {
        QMutexLocker lock(&egressMutex);
        ClearRoutesLocked();
        return {};
    }

    QString TearDown() {
        QMutexLocker lock(&egressMutex);
        ClearRoutesLocked();
        for (const char *rawName : {kConnectionName, kLegacyConnectionName}) {
            const QString name = QString::fromUtf8(rawName);
            if (ConnectionIsActive(name)) RunNmcli({QStringLiteral("connection"), QStringLiteral("down"), name}, 30000);
        }
        return {};
    }

    QString CurrentIface() { return ActivePppDevice(); }

    QStringList LastRoutedHosts() {
        QMutexLocker lock(&egressMutex);
        return routedHosts;
    }

    QString PrepareForProfile(const std::shared_ptr<Configs::Profile> &profile, bool tunMode) {
        const auto &settings = Configs::dataManager->settingsRepo;
        if (!settings->enable_pptp_egress) return {};
        if (tunMode || settings->spmode_vpn) {
            return QObject::tr("PPTP/L2TP egress cannot be used with Tun Mode. Turn off Tun or the egress tunnel.");
        }
        return ApplyProfileRoutes(profile);
    }

    void CleanupAfterProfile(bool switchingProfiles) {
        ClearProfileRoutes();
        const auto &settings = Configs::dataManager->settingsRepo;
        if (!switchingProfiles && !settings->enable_pptp_egress) TearDown();
    }

    QStringList SystemProxyIgnoreHosts() {
        const auto &settings = Configs::dataManager->settingsRepo;
        if (!settings->enable_pptp_egress && settings->pptp_gateway.trimmed().isEmpty()) return {};
        const auto gw = settings->pptp_gateway.trimmed();
        return gw.isEmpty() ? QStringList{} : QStringList{gw};
    }
}
