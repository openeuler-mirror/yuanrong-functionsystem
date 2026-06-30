# Implementation Plan: Generic LRU Cache Module

**Branch**: `001-generic-lru-module` | **Date**: 2026-02-15 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/001-generic-lru-module/spec.md`

**User guidance**: When data is no longer actively used, it is added to the
LRU queue; cache cleanup is triggered via an async eviction callback.

## Summary

Implement a generic, template-based LRU cache (`LruCache<K, V>`) under
`functionsystem/src/common/lru/` with O(1) operations, optional eviction
callbacks for async cleanup, and a separate `ThreadSafeLruCache<K, V>`
mutex wrapper. The design uses `std::list` + `std::unordered_map` for
O(1) access and eviction ordering. Data no longer in active use is
placed into the LRU eviction queue; when capacity is exceeded, the
eviction callback fires asynchronously to trigger cache cleanup (e.g.,
flushing snapshots to persistent storage).

## Technical Context

**Language/Version**: C++17 (CMake 3.16+, ccache enabled)
**Primary Dependencies**: STL only (`<list>`, `<unordered_map>`,
`<optional>`, `<mutex>`, `<functional>`); no external dependencies
**Storage**: N/A (in-memory only)
**Testing**: Google Test + Google Mock (existing project test framework)
**Target Platform**: Linux x86_64 (compile container)
**Project Type**: Single static library within monorepo
**Performance Goals**: O(1) amortized for all public API operations
**Constraints**: Header-only or header+source template library; must
compile with existing `.clang-format`; no external dependencies
**Scale/Scope**: Typical cache sizes: 100–10,000 entries

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Modular Component Architecture | ✅ PASS | New module under `common/lru/`, independently testable |
| II. gRPC Communication Protocol | N/A | Pure library, no inter-service communication |
| III. Container Runtime Abstraction | N/A | Not related |
| IV. Observability and Tracing | ✅ PASS | No built-in stats (per clarification); callers instrument externally |
| V. Test Coverage and Quality Gates | ✅ PASS | Unit tests planned with 10+ eviction scenarios + TSan |
| VI. Snapshot and Checkpoint Management | ✅ PASS | LRU supports snapshot use case via eviction callback |
| VII. LiteBus Actor Programming | ✅ PASS | Base class lock-free for actor use; separate wrapper for shared access |
| C++ Naming Conventions | ✅ PASS | small camelCase, `member_` prefix, ALL_CAPS constants |
| Build System (CMake) | ✅ PASS | Static library with `add_subdirectory()` pattern |

No violations. Complexity Tracking section not needed.

## Project Structure

### Documentation (this feature)

```text
specs/001-generic-lru-module/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (C++ API contracts)
│   └── lru_cache_api.h  # Public API header contract
└── checklists/
    └── requirements.md  # Spec quality checklist
```

### Source Code (repository root)

```text
functionsystem/src/common/lru/
├── CMakeLists.txt       # Static library build config
├── lru_cache.h          # LruCache<K,V> template (header-only core)
└── thread_safe_lru_cache.h  # ThreadSafeLruCache<K,V> mutex wrapper

functionsystem/tests/unit/common/lru/
└── lru_cache_test.cpp   # Unit tests (GTest)
```

**Structure Decision**: Header-only template library under `common/lru/`.
Since `LruCache` is a class template, the implementation MUST reside in
the header file. `ThreadSafeLruCache` is also header-only (thin wrapper).
The CMakeLists.txt creates an INTERFACE library (headers only, no .cpp).
This follows the project pattern where template-heavy modules are
header-only while non-template modules use static libraries.

## Phase 0: Research

See [research.md](./research.md) for detailed findings.

**Key decisions**:
1. **Data structure**: `std::list<pair<K,V>>` + `std::unordered_map<K, list::iterator>`
   — classic O(1) LRU with STL only
2. **Header-only**: Template class requires header-only implementation
3. **Eviction callback**: `std::function<void(const K&, V&&)>` — value is
   moved to callback so caller can take ownership for async cleanup
4. **Thread-safe wrapper**: `ThreadSafeLruCache` wraps all methods with
   `std::lock_guard<std::mutex>`
5. **Move-only support**: Values stored in `std::list` nodes (stable
   addresses), moved via `std::move` on eviction/retrieval

## Phase 1: Design

See [data-model.md](./data-model.md) for entity design.
See [contracts/lru_cache_api.h](./contracts/lru_cache_api.h) for API contract.
See [quickstart.md](./quickstart.md) for usage examples.

### API Design Summary

```cpp
template <typename K, typename V>
class LruCache {
public:
    using EvictionCallback = std::function<void(const K&, V&&)>;

    explicit LruCache(size_t capacity,
                      EvictionCallback onEvict = nullptr);

    // Promoting operations
    bool Put(const K& key, V value);
    std::optional<std::reference_wrapper<V>> Get(const K& key);

    // Non-promoting operations
    bool Contains(const K& key) const;
    std::optional<std::reference_wrapper<const V>> Peek(const K& key) const;

    // Mutation
    bool Remove(const K& key);
    void Clear();

    // Queries
    size_t Size() const;
    size_t Capacity() const;
};

template <typename K, typename V>
class ThreadSafeLruCache {
    // Same public API, internally delegates to LruCache with mutex
};
```

### Eviction Flow (per user guidance)

When data is no longer actively used and new entries are inserted:

1. `Put(key, value)` checks if `Size() >= Capacity()`
2. If capacity exceeded, the LRU entry (list back) is detached
3. The eviction callback is invoked with `(key, std::move(value))`
   — caller receives ownership of the value for async cleanup
4. The caller's callback can trigger async operations (e.g., via
   LiteBus `Async`/`Defer`) to flush data to persistent storage
5. The entry is removed from the internal map

This design enables the "add to LRU queue → async callback triggers
cleanup" pattern described in the user guidance.
