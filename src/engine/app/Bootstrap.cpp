#include "include/engine/app/Bootstrap.h"

#include <cstring>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

#include "include/engine/app/DataDirLock.h"
#include "include/engine/app/InstanceServer.h"
#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/stats/traffic/TrafficStatsManager.hpp"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"

#include <windows.h>
// <windows.h> defines SetPort, which under unity builds clobbers Configs::outbound::SetPort.
#undef SetPort
#endif
#ifdef Q_OS_UNIX
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

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

    void SetSoftwareNames() {
        software_name = "Throne";
        software_core_name = "sing-box";
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

    RelaunchParent::RelaunchParent(QStringList &arguments) {
        const QString prefix = "-wait_pid=";
        for (qsizetype i = arguments.size() - 1; i >= 1; i--) {
            if (arguments.at(i).startsWith(prefix)) pid = arguments.takeAt(i).mid(prefix.size()).toLongLong();
        }
#ifdef Q_OS_WIN
        if (pid > 0) {
            handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
            if (handle != nullptr) WaitForSingleObject(handle, 30000);
        }
#endif
    }

    RelaunchParent::~RelaunchParent() {
#ifdef Q_OS_WIN
        if (handle != nullptr) CloseHandle(handle);
#endif
    }

    bool RelaunchParent::gone() const {
        if (pid <= 0) return true;
#ifdef Q_OS_WIN
        return handle == nullptr || WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
#else
        return ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
    }

    void WarnDroppedPayload(const QStringList &payload) {
        if (!payload.isEmpty()) LOG_WARN("dropped the launch payload: " + payload.join(' '));
    }

    Claim ClaimDataDir(DataDirLock &lock, const DataDir &dataDir, const QString &serverName, const QString &lockMode,
                       const QStringList &payload, const RelaunchParent &relaunchParent) {
        const QString &configDir = dataDir.configDir;
        // For up to 60 s a restarted instance never hands off: the owner is its exiting parent, which still answers on the socket.
        const QDeadlineTimer relaunchDeadline(relaunchParent.relaunched() ? 60000 : 0);
        QDeadlineTimer handOffDeadline(QDeadlineTimer::Forever);
        while (true) {
            const auto lockResult = lock.tryAcquire(configDir, lockMode);
            if (lockResult == DataDirLock::Result::Failed) {
                LOG_WARN("running without the data dir lock: " + lock.errorString());
                break;
            }
            if (lockResult == DataDirLock::Result::Acquired) {
                if (!lock.errorString().isEmpty()) LOG_WARN(lock.errorString());
                break;
            }
            if (!relaunchDeadline.hasExpired() && !relaunchParent.gone()) {
                QThread::msleep(100);
                continue;
            }
            if (handOffDeadline.isForever()) {
                if (relaunchParent.relaunched() && !relaunchParent.gone()) {
                    LOG_WARN(QString("the data dir is still held after waiting for the restarting PID %1").arg(relaunchParent.pid));
                }
                handOffDeadline.setRemainingTime(10000);
            }
            // The owner may still be starting up, or exiting and about to release the lock.
            if (InstanceServer::HandOff(serverName, payload)) return {ClaimOutcome::HandedOff};
            if (handOffDeadline.hasExpired()) {
                const auto owner = DataDirLock::ReadOwner(configDir);
                const auto pid = owner.value("pid").toInteger();
                LOG_WARN(QString("the data dir is held by PID %1, which does not answer on %2").arg(QString::number(pid), serverName));
                WarnDroppedPayload(payload);
                return {ClaimOutcome::HeldByOther, pid};
            }
            QThread::msleep(100);
        }
        // A Throne older than the lock only answers on its socket; a restarted one would only find its exiting parent there.
        if (relaunchParent.gone() && InstanceServer::HandOff(serverName, payload)) return {ClaimOutcome::HandedOff};
        return {};
    }

    void OpenDataDir(const DataDir &dataDir, const QStringList &arguments) {
        const bool useAppdata = dataDir.useAppdata;
        const QString &appdataDir = dataDir.appdataDir;
        const QString &configDir = dataDir.configDir;

        QDir::setCurrent(configDir);
        QDir("temp").removeRecursively();
        AdoptLegacyGeoAssets(dataDir);

        appStartEpoch = QDateTime::currentSecsSinceEpoch();

        Configs::initDB(QString(QDir::currentPath() + QDir::separator() + "throne.db").toStdString());

        Logging::SetLevel(Logging::LevelFromString(Configs::dataManager->settingsRepo->log_file_level));

        Stats::trafficStatsManager->Init();

        Configs::dataManager->settingsRepo->argv = arguments;
        if (Configs::dataManager->settingsRepo->argv.contains("-many")) Configs::dataManager->settingsRepo->flag_many = true;
        if (Configs::dataManager->settingsRepo->argv.contains("-tray")) Configs::dataManager->settingsRepo->flag_tray = true;
        if (Configs::dataManager->settingsRepo->argv.contains("-debug")) Configs::dataManager->settingsRepo->flag_debug = true;
        if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_tun_on")) Configs::dataManager->settingsRepo->flag_restart_tun_on = true;
        Configs::dataManager->settingsRepo->flag_use_appdata = useAppdata;
        if(useAppdata && !appdataDir.isEmpty()) Configs::dataManager->settingsRepo->appdataDir = appdataDir;
#ifdef NKR_CPP_DEBUG
        Configs::dataManager->settingsRepo->flag_debug = true;
#endif
    }

    void PrepareRuntime() {
#ifdef Q_OS_LINUX
        QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/usr/plugins");
#endif

        DS_cores = new QThread;
        DS_cores->start();
    }

    void StartLogging(const DataDir &dataDir) {
        Logging::Init(dataDir.configDir);
        LOG_INFO(QString("appdata mode: %1").arg(dataDir.useAppdata ? "yes" : "no"));
#ifdef Q_OS_WIN
        Windows_SetCrashDumpPath();
        Windows_ConfigureWER();
#endif
    }

    bool ExitInProgress() {
        return Configs::dataManager != nullptr && Configs::dataManager->settingsRepo->prepare_exit;
    }

#ifdef Q_OS_UNIX
    namespace {
        int bootstrapSignalPipe[2] = {-1, -1};

        // Async-signal-safe: only the self-pipe write() is allowed here; teardown runs from the notifier on the main thread.
        void bootstrapPosixSignalHandler(int signum) {
            const auto byte = static_cast<char>(signum);
            [[maybe_unused]] const ssize_t written = ::write(bootstrapSignalPipe[1], &byte, 1);
        }
    } // namespace

    // Signals caught from here on wait in the pipe until ArmTerminationHandlers().
    bool InstallTerminationHandlers(void (*directFallback)(int)) {
        if (::pipe(bootstrapSignalPipe) != 0) {
            bootstrapSignalPipe[0] = bootstrapSignalPipe[1] = -1;
            if (directFallback != nullptr) {
                signal(SIGTERM, directFallback);
                signal(SIGINT, directFallback);
            }
            return false;
        }
        for (const int fd : bootstrapSignalPipe) {
            ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
            // Non-blocking: a full pipe must fail the write, never block in signal context.
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        }

        struct sigaction sa{};
        sa.sa_handler = bootstrapPosixSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        return true;
    }

    void ArmTerminationHandlers(std::function<void()> onSignal) {
        if (bootstrapSignalPipe[0] < 0) return;
        auto *notifier = new QSocketNotifier(bootstrapSignalPipe[0], QSocketNotifier::Read, QCoreApplication::instance());
        QObject::connect(notifier, &QSocketNotifier::activated, QCoreApplication::instance(), [notifier, onSignal = std::move(onSignal)] {
            char drain[16];
            while (::read(bootstrapSignalPipe[0], drain, sizeof(drain)) > 0) {}
            if (ExitInProgress()) return;
            notifier->setEnabled(false);
            onSignal();
        });
    }
#endif
} // namespace Engine::Bootstrap
