#pragma once

#include "Const.hpp"
#include "Utils.hpp"
#include "include/database/DatabaseManager.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace Configs {
    void initDB(const std::string& dbPath);

    QString FindCoreRealPath();

    bool IsAdmin(bool forceRenew=false);

    // The cached IsAdmin answer without an RPC or side effects; empty while unknown. Main thread.
    std::optional<bool> KnownAdmin();

    // Caches an answer IsAdmin would have fetched, such as the core's privilege probe. Main thread.
    void SetKnownAdmin(bool admin);

    bool isSetuidSet(const std::string& path);

    // Set once at startup to the resolved data dir; before that GetBasePath() falls back to the old guess.
    void SetBasePath(const QString &path);

    QString GetBasePath();
} // namespace Configs

#define ROUTES_PREFIX_NAME QString("route_profiles")
#define ROUTES_PREFIX QString(ROUTES_PREFIX_NAME + "/")
