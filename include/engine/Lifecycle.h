#pragma once

#include <functional>

namespace Engine::Lifecycle {
    using StopHandler = std::function<void(bool crash, bool block, bool manual)>;

    void SetStopHandler(StopHandler handler);

    // Runs the handler on the calling thread, never marshalled with a wait; a logged no-op while none is set.
    void StopRunningProfile(bool crash, bool block, bool manual);
} // namespace Engine::Lifecycle
