#pragma once

#include <optional>

#include <QList>

#include "include/engine/services/ServiceTypes.h"

namespace Engine::ProfileService {
    // Main thread. Why Delete would refuse now (an editor holds one of them).
    std::optional<ServiceError> CheckDelete(const QList<int> &profileIds);

    // Main thread, with none of them running. Deletes them, resets the front/landing proxies that pointed at them, and
    // emits SubscriptionGroupChanged per affected group (auto-selector pruning) and ProfilesRemoved.
    std::optional<ServiceError> Delete(const QList<int> &profileIds, int *deleted = nullptr);
} // namespace Engine::ProfileService
