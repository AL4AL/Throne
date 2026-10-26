#include "include/sys/linux/PptpEgress.h"

#include "include/configs/common/Outbound.h"
#include "include/configs/generate.h"
#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/global/Validation.hpp"
#include "include/sys/linux/LinuxCap.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QRecursiveMutex>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace {
    QRecursiveMutex pptpMutex;
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

    bool HasConnectionNamed(const QString &name) {
        const auto nmcli = NmcliPath();
        if (nmcli.isEmpty()) return false;
        QProcess p;
        p.start(nmcli, {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME"), QStringLiteral("connection"),
                        QStringLiteral("show")});
        if (!p.waitForFinished(10000)) return false;
        const auto lines = QString::fromLocal8Bit(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return lines.contains(name);
    }

    bool ConnectionIsActive(const QString &name) {
        const auto nmcli = NmcliPath();
        if (nmcli.isEmpty()) return false;
        QProcess p;
        p.start(nmcli, {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME"), QStringLiteral("connection"),
                        QStringLiteral("show"), QStringLiteral("--active")});
        if (!p.waitForFinished(10000)) return false;
        const auto lines = QString::fromLocal8Bit(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return lines.contains(name);
    }

    QString ActivePppDevice() {
        const auto nmcli = NmcliPath();
        if (!nmcli.isEmpty()) {
            QProcess p;
            p.start(nmcli, {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DEVICE,TYPE,CONNECTION"),
                            QStringLiteral("device"), QStringLiteral("status")});
            if (p.waitForFinished(10000)) {
                const auto lines = QString::fromLocal8Bit(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const auto &line : lines) {
                    const auto parts = line.split(QLatin1Char(':'));
                    if (parts.size() < 3) continue;
                    if (parts[2] != QLatin1String(Sys::PptpEgress::kConnectionName)) continue;
                    if (parts[0].startsWith(QStringLiteral("ppp"))) return parts[0];
                }
            }
        }
        QProcess ip;
        ip.start(IpPath(), {QStringLiteral("-br"), QStringLiteral("link"), QStringLiteral("show"), QStringLiteral("type"),
                            QStringLiteral("ppp")});
        if (!ip.waitForFinished(5000)) return {};
        const auto ipLines = QString::fromLocal8Bit(ip.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto &line : ipLines) {
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
            return QObject::tr("pkexec timed out while applying PPTP routes/MTU.");
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
        const auto info = QHostInfo::fromName(host);
        for (const auto &addr : info.addresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol || addr.protocol() == QAbstractSocket::IPv6Protocol) {
                const auto s = addr.toString();
                if (!ips.contains(s)) ips << s;
            }
        }
        return ips;
    }

    QString HostRouteSuffix(const QString &ip) {
        const QHostAddress addr(ip);
        if (addr.protocol() == QAbstractSocket::IPv6Protocol) return QStringLiteral("/128");
        return QStringLiteral("/32");
    }

    QString ClearRoutesLocked() {
        if (routedHosts.isEmpty()) return {};
        const auto ip = IpPath();
        QString script;
        for (const auto &addr : routedHosts) {
            script += QStringLiteral("%1 route del %2%3 dev %4 2>/dev/null || true\n")
                          .arg(ip, addr, HostRouteSuffix(addr),
                               routedIface.isEmpty() ? QStringLiteral("ppp0") : routedIface);
        }
        routedHosts.clear();
        routedIface.clear();
        if (!script.isEmpty()) PrivilegedIpScript(script);
        return {};
    }
}

namespace Sys::PptpEgress {
    QString CredentialsProblem() {
        const auto &s = Configs::dataManager->settingsRepo;
        if (s->pptp_gateway.trimmed().isEmpty()) return QObject::tr("PPTP server is not configured.");
        if (auto p = Validation::Host(s->pptp_gateway.trimmed()); !p.isEmpty()) return p;
        if (s->pptp_username.trimmed().isEmpty()) return QObject::tr("PPTP username is empty.");
        if (s->pptp_password.isEmpty()) return QObject::tr("PPTP password is empty.");
        if (s->pptp_mtu < 576 || s->pptp_mtu > 1500) return QObject::tr("PPTP MTU must be between 576 and 1500.");
        return {};
    }

    bool IsUp() { return ConnectionIsActive(QString::fromUtf8(kConnectionName)); }

    QString EnsureVpnUp() {
        QMutexLocker lock(&pptpMutex);
        if (const auto problem = CredentialsProblem(); !problem.isEmpty()) return problem;
        if (NmcliPath().isEmpty()) return QObject::tr("nmcli was not found; install NetworkManager.");

        const auto &s = Configs::dataManager->settingsRepo;
        const auto gateway = s->pptp_gateway.trimmed();
        const auto user = s->pptp_username.trimmed();
        const auto password = s->pptp_password;
        const int mtu = s->pptp_mtu;
        const QString name = QString::fromUtf8(kConnectionName);
        const QString vpnData = QStringLiteral("gateway=%1,user=%2,password-flags=0").arg(gateway, user);

        if (!HasConnectionNamed(name)) {
            if (auto err = RunNmcli({QStringLiteral("connection"), QStringLiteral("add"), QStringLiteral("type"),
                                    QStringLiteral("vpn"), QStringLiteral("con-name"), name, QStringLiteral("ifname"),
                                    QStringLiteral("*"), QStringLiteral("vpn-type"), QStringLiteral("pptp"),
                                    QStringLiteral("vpn.data"), vpnData, QStringLiteral("vpn.secrets"),
                                    QStringLiteral("password=") + password, QStringLiteral("ipv4.never-default"),
                                    QStringLiteral("yes"), QStringLiteral("ipv6.never-default"), QStringLiteral("yes")});
                !err.isEmpty()) {
                return QObject::tr("Could not create PPTP connection (%1). Install network-manager-pptp and pptp-linux.")
                    .arg(err);
            }
        } else if (auto err = RunNmcli({QStringLiteral("connection"), QStringLiteral("modify"), name,
                                       QStringLiteral("vpn.data"), vpnData, QStringLiteral("vpn.secrets"),
                                       QStringLiteral("password=") + password, QStringLiteral("ipv4.never-default"),
                                       QStringLiteral("yes"), QStringLiteral("ipv6.never-default"), QStringLiteral("yes")});
                   !err.isEmpty()) {
            return QObject::tr("Could not update PPTP connection: %1").arg(err);
        }

        if (!ConnectionIsActive(name)) {
            if (auto err = RunNmcli({QStringLiteral("connection"), QStringLiteral("up"), name}, 90000); !err.isEmpty()) {
                return QObject::tr("Could not bring up PPTP (%1). Install network-manager-pptp if needed.").arg(err);
            }
        }

        QString iface;
        for (int attempt = 0; attempt < 30; ++attempt) {
            iface = ActivePppDevice();
            if (!iface.isEmpty()) break;
            QThread::msleep(500);
        }
        if (iface.isEmpty()) return QObject::tr("PPTP connected but no ppp interface appeared.");

        const auto script = QStringLiteral("%1 link set dev %2 mtu %3").arg(IpPath(), iface, QString::number(mtu));
        if (auto err = PrivilegedIpScript(script); !err.isEmpty()) {
            return QObject::tr("Could not set PPTP MTU: %1").arg(err);
        }
        return {};
    }

    QString ApplyProfileRoutes(const std::shared_ptr<Configs::Profile> &profile) {
        if (auto err = EnsureVpnUp(); !err.isEmpty()) return err;

        QMutexLocker lock(&pptpMutex);
        ClearRoutesLocked();

        const auto host = DialHostForProfile(profile);
        if (host.isEmpty()) return QObject::tr("Profile has no server address to route via PPTP.");

        const auto ips = ResolveHostToIps(host);
        if (ips.isEmpty()) {
            return QObject::tr("Could not resolve profile server %1 for PPTP routing.").arg(host);
        }

        const auto iface = ActivePppDevice();
        if (iface.isEmpty()) return QObject::tr("No PPTP interface for host routes.");

        const auto ip = IpPath();
        QString script;
        for (const auto &addr : ips) {
            script += QStringLiteral("%1 route replace %2%3 dev %4\n").arg(ip, addr, HostRouteSuffix(addr), iface);
        }
        if (auto err = PrivilegedIpScript(script); !err.isEmpty()) {
            return QObject::tr("Could not install PPTP host routes: %1").arg(err);
        }
        routedHosts = ips;
        routedIface = iface;
        return {};
    }

    QString ClearProfileRoutes() {
        QMutexLocker lock(&pptpMutex);
        return ClearRoutesLocked();
    }

    QString TearDown() {
        QMutexLocker lock(&pptpMutex);
        ClearRoutesLocked();
        const QString name = QString::fromUtf8(kConnectionName);
        if (ConnectionIsActive(name)) {
            RunNmcli({QStringLiteral("connection"), QStringLiteral("down"), name}, 30000);
        }
        return {};
    }

    QString CurrentIface() { return ActivePppDevice(); }

    QStringList LastRoutedHosts() {
        QMutexLocker lock(&pptpMutex);
        return routedHosts;
    }
}
