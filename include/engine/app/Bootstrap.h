#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

namespace Engine::Bootstrap {
    enum class Mode {
        Gui,
        DumpConfigs,
    };

    // Runs before any Qt object exists, so it only looks at raw argv.
    Mode PreScan(int argc, char **argv);

    struct DataDir {
        QDir wd;
        bool useAppdata = false;
        // As given after -appdata; empty when the flag had no value.
        QString appdataDir;
        QString configDir;
    };

    // Sets the application name first, since AppConfigLocation depends on it. Creates wd and wd/config; a relative -appdata resolves against relativeBase.
    DataDir ResolveDataDir(const QStringList &arguments, const QDir &relativeBase);

    QString InstanceServerName(const QDir &wd);

    // An explicit -appdata dir used to keep its Xray geo assets in AppConfigLocation; copies the ones it lacks.
    void AdoptLegacyGeoAssets(const DataDir &dataDir);
} // namespace Engine::Bootstrap
