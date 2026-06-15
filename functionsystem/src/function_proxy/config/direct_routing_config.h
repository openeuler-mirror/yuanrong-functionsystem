#pragma once

#include <atomic>
#include <cstddef>

namespace functionsystem::function_proxy {

// Feature flag: enable direct routing via LRU cache and single-writer persistence.
// When false (default), the original etcd watch-based broadcast path is used.
// When true, the new direct routing path is activated.
class DirectRoutingConfig {
public:
    static constexpr size_t DEFAULT_ROUTE_CACHE_CAPACITY = 1024;
    static bool IsEnabled()
    {
        return enabled_.load(std::memory_order_relaxed);
    }

    static void SetEnabled(bool enabled)
    {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

    static size_t GetRouteCacheCapacity()
    {
        return routeCacheCapacity_.load(std::memory_order_relaxed);
    }

    static void SetRouteCacheCapacity(size_t capacity)
    {
        if (capacity == 0) {
            capacity = DEFAULT_ROUTE_CACHE_CAPACITY;
        }
        routeCacheCapacity_.store(capacity, std::memory_order_relaxed);
    }

    // RAII guard for unit tests: enables DR mode and restores to false on destruction.
    struct TestGuard {
        ~TestGuard()
        {
            SetEnabled(false);
            SetRouteCacheCapacity(DEFAULT_ROUTE_CACHE_CAPACITY);
        }
    };
    static TestGuard EnableForTest()
    {
        SetEnabled(true);
        return TestGuard{};
    }

private:
    inline static std::atomic<bool> enabled_{ false };
    inline static std::atomic<size_t> routeCacheCapacity_{ DEFAULT_ROUTE_CACHE_CAPACITY };
};

}  // namespace functionsystem::function_proxy
