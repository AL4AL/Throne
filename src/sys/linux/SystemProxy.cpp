#include "include/sys/SystemProxy.hpp"

#include <QMutex>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {
    QMutex proxyMutex;
    bool proxyApplied = false;
    // Snapshot of ignore-hosts / NoProxy taken before Throne merged extraIgnoreHosts; restored on Clear.
    bool ignoreHostsSaved = false;
    QString savedIgnoreHostsGsettings; // gsettings representation, e.g. "['localhost', '127.0.0.0/8']"
    QString savedNoProxyKde;

    const QStringList kReparseKio = {"--type=signal", "/KIO/Scheduler", "org.kde.KIO.Scheduler.reparseSlaveConfiguration", "string:''"};

    // GNOME's settings are written on every desktop, since GTK apps and Firefox read them under KDE too; KDE's only in KDE sessions.
    struct SettingsTools {
        QString gsettings = QStandardPaths::findExecutable("gsettings");
        QString kwriteconfig;

        SettingsTools() {
            const auto desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':');
            if (!desktops.contains("KDE") && !desktops.contains("Trinity")) return;
            for (const auto *name : {"kwriteconfig6", "kwriteconfig5", "kwriteconfig"}) {
                kwriteconfig = QStandardPaths::findExecutable(name);
                if (!kwriteconfig.isEmpty()) break;
            }
        }
    };

    void runSettingsTool(QStringList &errors, const QString &program, const QStringList &args) {
        QProcess process;
        process.start(program, args);
        if (!process.waitForFinished(10000)) {
            errors << program + ": " + process.errorString();
            return;
        }
        if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) return;
        const auto output = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        errors << program + " " + args.join(' ') + ": " + (output.isEmpty() ? QString("exit code %1").arg(process.exitCode()) : output);
    }

    QString runSettingsToolOutput(const QString &program, const QStringList &args) {
        QProcess process;
        process.start(program, args);
        if (!process.waitForFinished(10000)) return {};
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
        return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }

    QStringList kioslavercArgs(const QString &key, const QString &value) {
        return {"--file", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kioslaverc",
                "--group", "Proxy Settings", "--key", key, value};
    }

    // Parse gsettings array output like ['a', 'b'] into items.
    QStringList ParseGsettingsStringArray(const QString &raw) {
        QStringList out;
        auto s = raw.trimmed();
        if (s.startsWith(QLatin1Char('[')) && s.endsWith(QLatin1Char(']'))) s = s.mid(1, s.size() - 2);
        for (auto part : s.split(QLatin1Char(','))) {
            part = part.trimmed();
            if (part.startsWith(QLatin1Char('\'')) && part.endsWith(QLatin1Char('\''))) part = part.mid(1, part.size() - 2);
            if (part.startsWith(QLatin1Char('"')) && part.endsWith(QLatin1Char('"'))) part = part.mid(1, part.size() - 2);
            if (!part.isEmpty()) out << part;
        }
        return out;
    }

    QString FormatGsettingsStringArray(const QStringList &items) {
        QStringList quoted;
        for (const auto &item : items) {
            auto escaped = item;
            escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
            quoted << QStringLiteral("'%1'").arg(escaped);
        }
        return QStringLiteral("[%1]").arg(quoted.join(QStringLiteral(", ")));
    }

    QStringList MergeUnique(QStringList base, const QStringList &extra) {
        for (const auto &e : extra) {
            const auto t = e.trimmed();
            if (t.isEmpty() || base.contains(t)) continue;
            base << t;
        }
        return base;
    }
}

QString SystemProxy_Apply(const QString &host, int port, const QString &, const QStringList &extraIgnoreHosts) {
    QMutexLocker lock(&proxyMutex);
    const SettingsTools tools;
    if (tools.gsettings.isEmpty() && tools.kwriteconfig.isEmpty()) return "no proxy settings tool was found (gsettings, or kwriteconfig under KDE)";
    proxyApplied = true;

    QStringList errors;
    const auto portText = QString::number(port);
    if (!tools.gsettings.isEmpty()) {
        for (const QString protocol : {"http", "https", "ftp", "socks"}) {
            runSettingsTool(errors, tools.gsettings, {"set", "org.gnome.system.proxy." + protocol, "host", host});
            runSettingsTool(errors, tools.gsettings, {"set", "org.gnome.system.proxy." + protocol, "port", portText});
        }
        if (!extraIgnoreHosts.isEmpty()) {
            if (!ignoreHostsSaved) {
                savedIgnoreHostsGsettings =
                    runSettingsToolOutput(tools.gsettings, {"get", "org.gnome.system.proxy", "ignore-hosts"});
                ignoreHostsSaved = true;
            }
            auto hosts = ParseGsettingsStringArray(savedIgnoreHostsGsettings);
            hosts = MergeUnique(hosts, extraIgnoreHosts);
            runSettingsTool(errors, tools.gsettings,
                            {"set", "org.gnome.system.proxy", "ignore-hosts", FormatGsettingsStringArray(hosts)});
        }
        runSettingsTool(errors, tools.gsettings, {"set", "org.gnome.system.proxy", "mode", "manual"});
    }
    if (!tools.kwriteconfig.isEmpty()) {
        const auto address = (host.contains(':') ? QString("[%1]").arg(host) : host) + " " + portText;
        for (const QString protocol : {"http", "https", "ftp"}) {
            runSettingsTool(errors, tools.kwriteconfig, kioslavercArgs(protocol + "Proxy", "http://" + address));
        }
        runSettingsTool(errors, tools.kwriteconfig, kioslavercArgs("socksProxy", "socks://" + address));
        if (!extraIgnoreHosts.isEmpty()) {
            const auto kread = [&]() -> QString {
                for (const auto *name : {"kreadconfig6", "kreadconfig5", "kreadconfig"}) {
                    const auto exec = QStandardPaths::findExecutable(name);
                    if (exec.isEmpty()) continue;
                    return runSettingsToolOutput(
                        exec, {"--file", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kioslaverc",
                               "--group", "Proxy Settings", "--key", "NoProxyFor"});
                }
                return {};
            }();
            if (!ignoreHostsSaved) {
                savedNoProxyKde = kread;
                ignoreHostsSaved = true;
            }
            QStringList noProxy = savedNoProxyKde.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (auto &n : noProxy) n = n.trimmed();
            noProxy = MergeUnique(noProxy, extraIgnoreHosts);
            runSettingsTool(errors, tools.kwriteconfig, kioslavercArgs("NoProxyFor", noProxy.join(QLatin1Char(','))));
        }
        runSettingsTool(errors, tools.kwriteconfig, kioslavercArgs("ProxyType", "1"));
        runSettingsTool(errors, "dbus-send", kReparseKio);
    }
    return errors.join("; ");
}

QString SystemProxy_Clear() {
    QMutexLocker lock(&proxyMutex);
    if (!proxyApplied) return {};
    const SettingsTools tools;

    QStringList errors;
    if (!tools.gsettings.isEmpty()) {
        runSettingsTool(errors, tools.gsettings, {"set", "org.gnome.system.proxy", "mode", "none"});
        if (ignoreHostsSaved && !savedIgnoreHostsGsettings.isEmpty()) {
            runSettingsTool(errors, tools.gsettings,
                            {"set", "org.gnome.system.proxy", "ignore-hosts", savedIgnoreHostsGsettings});
        }
    }
    if (!tools.kwriteconfig.isEmpty()) {
        runSettingsTool(errors, tools.kwriteconfig, kioslavercArgs("ProxyType", "0"));
        if (ignoreHostsSaved) {
            runSettingsTool(errors, tools.kwriteconfig, kioslavercArgs("NoProxyFor", savedNoProxyKde));
        }
        runSettingsTool(errors, "dbus-send", kReparseKio);
    }
    if (errors.isEmpty()) {
        proxyApplied = false;
        ignoreHostsSaved = false;
        savedIgnoreHostsGsettings.clear();
        savedNoProxyKde.clear();
    }
    return errors.join("; ");
}
