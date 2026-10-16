#include <csignal>
#include <cstdio>
#include <memory>

#include <QApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QTranslator>
#include <QMessageBox>
#include <QLocalSocket>
#include <QLocalServer>
#include <QThread>
#include <QDateTime>
#include <3rdparty/WinCommander.hpp>


#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/global/Notice.hpp"

#include "include/engine/app/Bootstrap.h"
#include "include/engine/app/ConfigDump.h"
#include "include/engine/app/DataDirLock.h"
#include "include/ui/mainwindow.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/api/RPC.h"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"
#include "include/sys/windows/eventHandler.h"
#include "include/sys/windows/WinVersion.h"
#include <qfontdatabase.h>
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <include/sys/linux/coreDump.h>
#include <qfontdatabase.h>
#endif
#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#ifdef Q_OS_MACOS
#include <QFileOpenEvent>

// macOS reuses the running app and delivers throne:// URLs and opened files as a QFileOpenEvent, never via argv.
class MacOpenEventFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            const auto openEvent = static_cast<QFileOpenEvent *>(event);
            const QString url = openEvent->url().toString();
            if (url.startsWith("throne://")) {
                Deeplink_Submit(url);
                return true;
            }
            const QString file = openEvent->file().isEmpty() ? openEvent->url().toLocalFile() : openEvent->file();
            if (!file.isEmpty()) {
                LaunchFiles_Submit({file});
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
#endif

namespace {
    bool exit_in_progress() {
        return Configs::dataManager != nullptr && Configs::dataManager->settingsRepo->prepare_exit;
    }
}

void signal_handler(int signum) {
    Q_UNUSED(signum)
    // The running teardown quits on its own; re-entering it from one of its nested loops would deadlock on mu_exit.
    if (exit_in_progress()) return;
    if (auto *mw = GetMainWindow()) mw->prepare_exit();
    qApp->quit();
}

#ifdef Q_OS_UNIX
namespace {
    int g_signalPipe[2] = {-1, -1};

    // Async-signal-safe: only the self-pipe write() is allowed here; teardown runs from the notifier on the main thread.
    void posix_signal_handler(int signum) {
        const auto byte = static_cast<char>(signum);
        [[maybe_unused]] const ssize_t written = ::write(g_signalPipe[1], &byte, 1);
    }

    // Signals caught from here on wait in the pipe until arm_termination_handlers().
    void install_termination_handlers() {
        if (::pipe(g_signalPipe) != 0) {
            g_signalPipe[0] = g_signalPipe[1] = -1;
            signal(SIGTERM, signal_handler);
            signal(SIGINT, signal_handler);
            return;
        }
        for (const int fd : g_signalPipe) {
            ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
            // Non-blocking: a full pipe must fail the write, never block in signal context.
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        }

        struct sigaction sa{};
        sa.sa_handler = posix_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }

    // Only right before exec(): a nested loop ahead of it would consume the signal while quit() is still a no-op.
    void arm_termination_handlers() {
        if (g_signalPipe[0] < 0) return;
        auto *notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, qApp);
        QObject::connect(notifier, &QSocketNotifier::activated, qApp, [notifier] {
            char drain[16];
            while (::read(g_signalPipe[0], drain, sizeof(drain)) > 0) {}
            if (exit_in_progress()) return;
            notifier->setEnabled(false);
            signal_handler(0);
        });
    }
}
#endif

QTranslator* trans = nullptr;
QTranslator* trans_qt = nullptr;

void loadTranslate(const QString& locale) {
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes");
    QT_TRANSLATE_NOOP("QPlatformTheme", "No");
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK");
    if (trans != nullptr) {
        trans->deleteLater();
    }
    if (trans_qt != nullptr) {
        trans_qt->deleteLater();
    }
    trans = new QTranslator;
    trans_qt = new QTranslator;
    QLocale::setDefault(QLocale(locale));
    const QString diskPath = QCoreApplication::applicationDirPath()+"/translations/" + locale + ".qm";
    const QString qrcPath = ":/translations/" + locale + ".qm";
    bool loadOK=false;
    if (QFileInfo::exists(diskPath)) {
        loadOK = trans->load(diskPath);
    }
    if (!loadOK) {
        loadOK = trans->load(qrcPath);
    }
    if (loadOK) {
        QCoreApplication::installTranslator(trans);
    }
}

namespace {
    // The instance that relaunched us (-wait_pid=); it keeps the data dir lock and the socket until it exits.
    class RelaunchParent {
    public:
        // Stripped on every OS so the flag never lands in the saved argv; Windows also waits up to 30 s for the exit.
        explicit RelaunchParent(QStringList &arguments) {
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

        ~RelaunchParent() {
#ifdef Q_OS_WIN
            if (handle != nullptr) CloseHandle(handle);
#endif
        }

        RelaunchParent(const RelaunchParent &) = delete;
        RelaunchParent &operator=(const RelaunchParent &) = delete;

        bool relaunched() const { return pid > 0; }

        // Once it is gone, whoever holds the lock is another instance, so the normal hand-off applies again.
        bool gone() const {
            if (pid <= 0) return true;
#ifdef Q_OS_WIN
            return handle == nullptr || WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
#else
            return ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
        }

        qint64 pid = 0;

    private:
#ifdef Q_OS_WIN
        HANDLE handle = nullptr;
#endif
    };

    // Framing is one url per line, so paths go over as file:// urls: a newline in a name would break it.
    bool HandOffToRunningInstance(const QString &serverName, const QStringList &payload) {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (!socket.waitForConnected(250)) return false;
        qDebug() << "Another instance is running, let's wake it up and quit";
        if (!payload.isEmpty()) {
            socket.write(payload.join('\n').toUtf8());
            socket.flush();
            socket.waitForBytesWritten(250);
        }
        socket.disconnectFromServer();
        return true;
    }

#ifdef Q_OS_WIN
    // Flags carry over; the deeplink and files go last as absolute paths, since the working directory has moved since launch.
    QStringList ElevatedRelaunchArguments(const QStringList &arguments, const QString &deeplink, const QStringList &files) {
        QStringList out;
        for (qsizetype i = 1; i < arguments.size(); i++) {
            const auto &arg = arguments.at(i);
            if (!arg.startsWith('-')) continue;
            out << arg;
            if (arg == "-appdata" && i + 1 < arguments.size() && !arguments.at(i + 1).startsWith('-')) out << arguments.at(++i);
        }
        out << QString("-wait_pid=%1").arg(QCoreApplication::applicationPid());
        if (!deeplink.isEmpty()) out << deeplink;
        return out + files;
    }
#endif
}


int main(int argc, char* argv[]) {
    Logging::InstallQtMessageHandler();

    // Ahead of the crash handler: its MessageBoxW would hang a displayless run.
    if (Engine::Bootstrap::PreScan(argc, argv) == Engine::Bootstrap::Mode::DumpConfigs) return Engine::RunConfigDump(argc, argv);

#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif
#ifdef Q_OS_LINUX
    enable_core_dumps();
#endif

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication a(argc, argv);

    SetNoticeSink([](const Notice &n) { ShowPassiveWarning(n.title, n.text); });

#ifdef Q_OS_MACOS
    // Install before the event loop so launch-by-deeplink FileOpen events are caught.
    a.installEventFilter(new MacOpenEventFilter(&a));
#endif

#if !defined(Q_OS_MACOS) && (QT_VERSION >= QT_VERSION_CHECK(6,9,0))
#ifdef Q_OS_WIN
    int fontId = QFontDatabase::addApplicationFont(WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_11_22H2) ? ":/font/notoEmoji" : ":/font/Twemoji");
#else
    int fontId = QFontDatabase::addApplicationFont(":/font/notoEmoji");
#endif
    if (fontId >= 0)
    {
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        QFontDatabase::setApplicationEmojiFontFamilies(fontFamilies);
    } else
    {
        qDebug() << "could not load emoji font!";
    }
#endif

    QStringList arguments = QApplication::arguments();
    const RelaunchParent relaunchParent(arguments);
    // Must run before the working directory moves below: argument paths may be relative to it.
    const QString launchDeeplink = Deeplink_ExtractFromArgs(arguments);
    const QStringList launchFiles = LaunchFiles_ExtractFromArgs(arguments, QDir::current());

    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }

    const auto dataDir = Engine::Bootstrap::ResolveDataDir(arguments, QDir(QCoreApplication::applicationDirPath()));
    const QDir &wd = dataDir.wd;
    const bool useAppdata = dataDir.useAppdata;
    const QString &appdataDir = dataDir.appdataDir;
    const QString &configDir = dataDir.configDir;
    Configs::SetBasePath(wd.absolutePath());

    const auto serverName = Engine::Bootstrap::InstanceServerName(wd);
    qDebug() << "server name: " << serverName;
    QStringList launchPayload;
    if (!launchDeeplink.isEmpty()) launchPayload << launchDeeplink;
    for (const auto &file : launchFiles) launchPayload << QUrl::fromLocalFile(file).toString();
    const auto warnDroppedPayload = [&launchPayload] {
        if (!launchPayload.isEmpty()) LOG_WARN("dropped the launch payload: " + launchPayload.join(' '));
    };

    // Before the temp wipe and initDB: the owner of the lock owns the data dir.
    Engine::DataDirLock dataDirLock;
    // For up to 60 s a restarted instance never hands off: the owner is its exiting parent, which still answers on the socket.
    const QDeadlineTimer relaunchDeadline(relaunchParent.relaunched() ? 60000 : 0);
    QDeadlineTimer handOffDeadline(QDeadlineTimer::Forever);
    while (true) {
        const auto lockResult = dataDirLock.tryAcquire(configDir, "gui");
        if (lockResult == Engine::DataDirLock::Result::Failed) {
            LOG_WARN("running without the data dir lock: " + dataDirLock.errorString());
            break;
        }
        if (lockResult == Engine::DataDirLock::Result::Acquired) {
            if (!dataDirLock.errorString().isEmpty()) LOG_WARN(dataDirLock.errorString());
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
        if (HandOffToRunningInstance(serverName, launchPayload)) return 0;
        if (handOffDeadline.hasExpired()) {
            const auto owner = Engine::DataDirLock::ReadOwner(configDir);
            const auto pid = owner.value("pid").toInteger();
            LOG_WARN(QString("the data dir is held by PID %1, which does not answer on %2").arg(QString::number(pid), serverName));
            warnDroppedPayload();
            // An autostart must not leave a box behind.
            if (!arguments.contains("-tray")) {
                const QString who = pid > 0 ? QObject::tr("Another Throne instance (PID %1)").arg(pid) : QObject::tr("Another Throne instance");
                QMessageBox::warning(nullptr, "Throne", QObject::tr("%1 is using this data directory:\n%2").arg(who, QDir::toNativeSeparators(wd.absolutePath())));
            }
            return 0;
        }
        QThread::msleep(100);
    }
    // A Throne older than the lock only answers on its socket; a restarted one would only find its exiting parent there.
    if (relaunchParent.gone() && HandOffToRunningInstance(serverName, launchPayload)) return 0;

    // Listening before initDB lets a second launch hand off while migrations run; its payload waits in the pending buffers.
    QLocalServer server(qApp);
    server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!server.listen(serverName)) {
        // Logging::Init has not run yet, so the file log never sees this.
        const QString error = QString("Failed to start the instance server %1: %2").arg(serverName, server.errorString());
        std::fputs(qPrintable(error + '\n'), stderr);
        warnDroppedPayload();
        QMessageBox::critical(nullptr, "Throne", error);
        return 1;
    }
    // Not before this point: until the data dir is ours there is nothing to tear down, and the default action ends the waits above.
#ifdef Q_OS_UNIX
    install_termination_handlers();
#endif
    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        auto s = server.nextPendingConnection();
        qDebug() << "Another instance tried to wake us up on " << serverName << s;
        // One url per line; the tail carries no trailing newline, so it is only flushed on disconnect.
        auto pending = std::make_shared<QByteArray>();
        auto handleLine = [](const QString &line) {
            if (line.startsWith("throne://")) {
                Deeplink_Submit(line);
            } else if (line.startsWith("file://")) {
                LaunchFiles_Submit({QUrl(line).toLocalFile()});
            }
        };
        auto readPayload = [s, pending, handleLine](bool last) {
            pending->append(s->readAll());
            while (true) {
                const auto at = pending->indexOf('\n');
                if (at < 0) break;
                handleLine(QString::fromUtf8(pending->first(at)).trimmed());
                pending->remove(0, at + 1);
            }
            if (last) {
                handleLine(QString::fromUtf8(*pending).trimmed());
                pending->clear();
            }
        };
        QObject::connect(s, &QLocalSocket::readyRead, s, [readPayload] { readPayload(false); });
        QObject::connect(s, &QLocalSocket::disconnected, s, [readPayload] { readPayload(true); });
        QObject::connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        readPayload(false); // in case the payload already arrived
        MW_dialog_message(MwMessage::Raise, {});
    });
    QObject::connect(qApp, &QApplication::aboutToQuit, &server, [&]
    {
        server.close();
        QLocalServer::removeServer(serverName);
        // Every quit path lands here; missing it is reported as a crash next start.
        Logging::Shutdown();
    });

    QDir::setCurrent(configDir);
    QDir("temp").removeRecursively();
    Engine::Bootstrap::AdoptLegacyGeoAssets(dataDir);

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

#ifdef Q_OS_LINUX
    QApplication::addLibraryPath(QApplication::applicationDirPath() + "/usr/plugins");
#endif

    DS_cores = new QThread;
    DS_cores->start();

    LogThread = new QThread;
    LogThread->start();

    QIcon::setFallbackSearchPaths(QStringList{
        ":/icon",
    });

    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName("breeze");
    }

#ifdef Q_OS_WIN
    if (Configs::dataManager->settingsRepo->windows_set_admin && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin)
    {
        Configs::dataManager->settingsRepo->windows_set_admin = false; // so that if permission denied, we will run as user on the next run
        Configs::dataManager->settingsRepo->Save();
        // Hand-offs already accepted are read into the pending buffers first; closing the server would drop them.
        const QDeadlineTimer pump(200);
        while (!pump.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        // Not listening during the UAC prompt, so a new launch waits for the elevated child; a declined prompt carries on as user.
        server.close();
        const QString pendingDeeplink = Deeplink_TakePending();
        const QStringList pendingFiles = LaunchFiles_TakePending();
        if (!launchDeeplink.isEmpty() && !pendingDeeplink.isEmpty()) LOG_WARN("dropped the handed-off deeplink: " + pendingDeeplink);
        const auto elevatedArguments = ElevatedRelaunchArguments(arguments, launchDeeplink.isEmpty() ? pendingDeeplink : launchDeeplink,
                                                                 launchFiles + pendingFiles);
        if (WinCommander::runProcessElevated(QApplication::applicationFilePath(), elevatedArguments, "", 1, false) == 0) return 0;
        if (!server.listen(serverName)) LOG_WARN("could not restart the instance server: " + server.errorString());
        Deeplink_Submit(pendingDeeplink);
        LaunchFiles_Submit(pendingFiles);
    }
#endif

    if (Configs::dataManager->settingsRepo->start_minimal) Configs::dataManager->settingsRepo->flag_tray = true;

    QString locale;
    switch (Configs::dataManager->settingsRepo->language) {
        case 1: // English
            break;
        case 2:
            locale = "zh_CN";
            break;
        case 3:
            locale = "fa_IR";
            break;
        case 4:
            locale = "ru_RU";
            break;
        default:
            locale = QLocale().name();
    }
    QGuiApplication::tr("QT_LAYOUT_DIRECTION");
    loadTranslate(locale);

    // Must follow the single-instance check: opening the log earlier truncates the running instance's file and fakes a crash marker.
    Logging::Init(configDir);
    LOG_INFO(QString("appdata mode: %1").arg(useAppdata ? "yes" : "no"));
#ifdef Q_OS_WIN
    Windows_SetCrashDumpPath();
    Windows_ConfigureWER();

    auto eventFilter = new PowerOffTaskkillFilter(signal_handler);
    a.installNativeEventFilter(eventFilter);
#endif

#ifdef Q_OS_MACOS
    QObject::connect(qApp, &QGuiApplication::commitDataRequest, [&](QSessionManager &manager)
    {
        Q_UNUSED(manager);
        signal_handler(0);
    });
#endif

    API::defaultClient = new API::Client();

    UI_InitMainWindow();

    Configs::dataManager->RunDeferredMaintenance();

    if (Logging::PreviousSessionCrashed()) {
        MW_show_log(QObject::tr("[Warn] Throne did not shut down cleanly last time. "
                                "Diagnostics were saved to: %1").arg(Logging::LogDir()));
    }

    // The Flush calls replay whatever arrived before the window existed (e.g. a macOS FileOpen event).
    if (!launchDeeplink.isEmpty()) Deeplink_Submit(launchDeeplink);
    Deeplink_FlushPending();
    LaunchFiles_Submit(launchFiles);
    LaunchFiles_FlushPending();

#ifdef Q_OS_UNIX
    arm_termination_handlers();
#endif
    return QApplication::exec();
}
