#pragma once

#include <atomic>

namespace functionsystem::function_proxy {

// Feature flag: enable direct routing via LRU cache and single-writer persistence.
// When false (default), the original etcd watch-based broadcast path is used.
// When true, the new direct routing path is activated.
class DirectRoutingConfig {
public:
    static bool IsEnabled()
    {
        return enabled_.load(std::memory_order_relaxed);
    }

    static void SetEnabled(bool enabled)
    {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

    // RAII guard for unit tests: enables DR mode and restores to false on destruction.
    struct TestGuard {
        ~TestGuard() { SetEnabled(false); }
    };
    static TestGuard EnableForTest()
    {
        SetEnabled(true);
        return TestGuard{};
    }

private:
    inline static std::atomic<bool> enabled_{ false };
};

}  // namespace functionsystem::function_proxy
