#include "include/engine/app/Bootstrap.h"

#include <cstring>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>

#include "include/global/Logger.hpp"

#define LOCAL_SERVER_PREFIX "throne-"

namespace Engine::Bootstrap {
    namespace {
        constexpr auto FALLBACK_MARKER = "config/.install-dir-unwritable";

        // QFileInfo::isWritable reports the read-only attribute, not what a UAC-filtered token can actually do.
        bool DirIsWritable(const QDir &dir) {
            if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return false;
            QFile probe(dir.absoluteFilePath(".throne-write-test"));
            if (!probe.open(QIODevice::WriteOnly)) return false;
            probe.close();
            probe.remove();
            return true;
        }

        bool ConfigDirIsUsable(const QDir &configDir) {
            if (!DirIsWritable(configDir)) return false;
            const QString db = configDir.absoluteFilePath("throne.db");
            if (!QFile::exists(db)) return true;
            QFile file(db);
            return file.open(QIODevice::ReadWrite);
        }

        void CopyDirContents(const QString &from, const QString &to) {
            QDir().mkpath(to);
            QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
            while (it.hasNext()) {
                it.next();
                const QString target = QDir(to).absoluteFilePath(it.fileName());
                if (it.fileInfo().isDir()) CopyDirContents(it.filePath(), target);
                else if (!QFile::exists(target)) QFile::copy(it.filePath(), target);
            }
        }

        // An elevated relaunch finds the install dir writable again, so the fallback must stay pinned by the marker.
        bool AdoptUserConfigDir(const QDir &installWd, const QDir &userWd) {
            QFile marker(userWd.absoluteFilePath(FALLBACK_MARKER));
            if (marker.open(QIODevice::ReadOnly)) {
                const bool pinnedHere = QString::fromUtf8(marker.readAll()).trimmed() == installWd.absolutePath();
                marker.close();
                if (pinnedHere) return true;
            }

            const QString installConfig = installWd.absoluteFilePath("config");
            if (ConfigDirIsUsable(QDir(installConfig))) return false;

            const QString userConfig = userWd.absoluteFilePath("config");
            QDir().mkpath(userConfig);
            if (!QFile::exists(userConfig + "/throne.db") && QFile::exists(installConfig + "/throne.db")) {
                CopyDirContents(installConfig, userConfig);
                LOG_WARN(QString("copied existing config from %1").arg(installConfig));
            }
            if (marker.open(QIODevice::WriteOnly)) {
                marker.write(installWd.absolutePath().toUtf8());
                marker.close();
            }
            LOG_WARN(QString("%1 is not writable, using %2").arg(installConfig, userConfig));
            return true;
        }
    } // namespace

    Mode PreScan(int argc, char **argv) {
        for (int i = 1; i < argc; i++) {
            if (argv[i] != nullptr && std::strcmp(argv[i], "-dump-configs") == 0) return Mode::DumpConfigs;
        }
        return Mode::Gui;
    }

    DataDir ResolveDataDir(const QStringList &arguments, const QDir &relativeBase) {
        DataDir res;
        res.wd = QDir(QCoreApplication::applicationDirPath());
        if (arguments.contains("-appdata")) {
            res.useAppdata = true;
            const auto appdataIndex = arguments.indexOf("-appdata");
            if (arguments.size() > appdataIndex + 1 && !arguments.at(appdataIndex + 1).startsWith("-")) {
                res.appdataDir = arguments.at(appdataIndex + 1);
            }
        }
#ifdef NKR_CPP_USE_APPDATA
        res.useAppdata = true;
#endif
        QCoreApplication::setApplicationName("Throne");
        if (res.useAppdata) {
            if (!res.appdataDir.isEmpty()) {
                res.wd.setPath(relativeBase.absoluteFilePath(res.appdataDir));
            } else {
                res.wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
            }
        } else {
            const QDir userWd(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
            if (AdoptUserConfigDir(res.wd, userWd)) {
                res.wd = userWd;
                res.useAppdata = true;
            }
        }
        if (!res.wd.exists()) res.wd.mkpath(res.wd.absolutePath());
        if (!res.wd.exists("config")) res.wd.mkdir("config");
        res.configDir = res.wd.absoluteFilePath("config");
        return res;
    }

    QString InstanceServerName(const QDir &wd) {
        QByteArray hashBytes = QCryptographicHash::hash(wd.absolutePath().toUtf8(), QCryptographicHash::Md5).toBase64(QByteArray::OmitTrailingEquals);
        hashBytes.replace('+', '0').replace('/', '1');
        return LOCAL_SERVER_PREFIX + QString::fromUtf8(hashBytes);
    }

    void AdoptLegacyGeoAssets(const DataDir &dataDir) {
        if (!dataDir.useAppdata || dataDir.appdataDir.isEmpty()) return;
        const QDir legacy(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        if (legacy.absolutePath() == dataDir.wd.absolutePath()) return;
        for (const QString &name : QStringList{"geoip.dat", "geosite.dat"}) {
            const QString from = legacy.absoluteFilePath(name);
            const QString to = dataDir.wd.absoluteFilePath(name);
            if (!QFile::exists(from) || QFile::exists(to)) continue;
            if (QFile::copy(from, to)) {
                LOG_INFO(QString("copied %1 from %2").arg(name, legacy.absolutePath()));
            } else {
                LOG_WARN(QString("could not copy %1 from %2").arg(name, legacy.absolutePath()));
            }
        }
    }
} // namespace Engine::Bootstrap
