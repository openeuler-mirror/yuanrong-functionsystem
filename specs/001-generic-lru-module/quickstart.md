# Quickstart: Generic LRU Cache Module

**Date**: 2026-02-15
**Feature**: 001-generic-lru-module

## Include

```cpp
#include "common/lru/lru_cache.h"
#include "common/lru/thread_safe_lru_cache.h"  // if thread safety needed
```

## Basic Usage

```cpp
// Create a cache with capacity 100
LruCache<std::string, int> cache(100);

// Insert entries
cache.Put("alpha", 1);
cache.Put("beta", 2);
cache.Put("gamma", 3);

// Retrieve (promotes to MRU)
auto result = cache.Get("alpha");
if (result) {
    int value = result->get();  // value == 1
}

// Check existence without promoting
bool exists = cache.Contains("beta");  // true

// Peek without promoting
auto peeked = cache.Peek("gamma");
if (peeked) {
    const int& value = peeked->get();  // value == 3
}

// Remove explicitly
cache.Remove("beta");

// Query state
size_t size = cache.Size();        // 2
size_t capacity = cache.Capacity(); // 100
```

## Eviction Callback (Async Cleanup Pattern)

When data is no longer actively used and gets evicted, trigger async
cleanup via the eviction callback:

```cpp
// In a LiteBus actor context
LruCache<std::string, std::unique_ptr<SnapshotData>> snapshotCache(
    1000,
    [this](const std::string& snapshotId,
           std::unique_ptr<SnapshotData>&& data) {
        // Async cleanup: flush snapshot to persistent storage
        // NOTE: In actor context, use Defer pattern instead of
        // capturing 'this' — shown here for simplicity
        FlushToPersistentStorage(snapshotId, std::move(data));
    }
);

// When a snapshot is accessed, it stays in cache (MRU)
auto snap = snapshotCache.Get("snap-001");

// When capacity is exceeded on new Put, the LRU snapshot is evicted
// and the callback triggers async cleanup automatically
snapshotCache.Put("snap-new", std::make_unique<SnapshotData>(...));
```

## Thread-Safe Usage

```cpp
// Wrap with mutex for multi-threaded access
ThreadSafeLruCache<std::string, RouteInfo> routeCache(500);

// All operations are thread-safe
routeCache.Put("instance-001", RouteInfo{...});
auto route = routeCache.Get("instance-001");
```

## Instance Routing Cache Example

```cpp
struct RouteInfo {
    std::string nodeAddress;
    uint16_t port;
    uint64_t version;
};

// Used in instance_view module
LruCache<std::string, RouteInfo> instanceRouteCache(
    2000,
    [](const std::string& instanceId, RouteInfo&& route) {
        LOG_INFO("Route evicted for instance: {}", instanceId);
    }
);

// Non-promoting lookup for diagnostics
if (instanceRouteCache.Contains("instance-xyz")) {
    auto route = instanceRouteCache.Peek("instance-xyz");
    // Inspect without affecting eviction order
}
```

## CMake Integration

In your module's `CMakeLists.txt`:

```cmake
target_link_libraries(your_module PRIVATE lru)
```

## Build & Test

```bash
# Build
bash run.sh build -j 4

# Run unit tests
bash run.sh test
```
