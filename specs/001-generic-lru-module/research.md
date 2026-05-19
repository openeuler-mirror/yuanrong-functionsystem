# Research: Generic LRU Cache Module

**Date**: 2026-02-15
**Feature**: 001-generic-lru-module

## R1: LRU Data Structure Choice

**Decision**: `std::list<std::pair<K,V>>` + `std::unordered_map<K, typename std::list<...>::iterator>`

**Rationale**: This is the canonical O(1) LRU implementation in C++.
`std::list` provides stable iterators (splice is O(1) for promotion),
and `std::unordered_map` provides O(1) key lookup. Together they
satisfy SC-004 (O(1) amortized for all operations).

**Alternatives considered**:
- **Intrusive list + custom hash map**: Better cache locality but
  requires custom allocator; over-engineering for initial version.
- **Boost.MultiIndex**: Provides ordered+hashed access but adds
  external dependency (violates zero-dependency constraint).
- **`std::deque` + linear scan**: O(n) promotion; rejected.

## R2: Header-Only vs Static Library

**Decision**: Header-only (INTERFACE CMake library)

**Rationale**: `LruCache<K,V>` is a class template. C++ requires
template definitions to be visible at instantiation site. Splitting
into `.h`/`.cpp` would require explicit instantiation for each type
combo, defeating the "generic" purpose.

**Alternatives considered**:
- **Explicit instantiation in .cpp**: Would require pre-declaring all
  K/V type combinations; not generic.
- **`.tpp` include pattern**: Adds unnecessary file indirection;
  single header is simpler and idiomatic for small templates.

## R3: Eviction Callback Signature

**Decision**: `std::function<void(const K&, V&&)>`

**Rationale**: The value is moved (rvalue ref) to the callback so the
caller can take ownership without copying. This supports the user's
use case: "when data is no longer used, add to LRU queue, trigger
async cleanup via callback." The callback receives the evicted value
by move, enabling it to forward to an async LiteBus operation (e.g.,
`Async(GetAID(), Defer(GetAID(), &Actor::FlushSnapshot, std::move(value)))`)
without additional copies.

Key is passed by const ref since the caller typically only needs it
for logging/routing, not ownership.

**Alternatives considered**:
- `std::function<void(K, V)>`: Copies key unnecessarily.
- `std::function<void(std::pair<K,V>&&)>`: Less readable API.
- Virtual interface (strategy pattern): Heavier; `std::function` is
  sufficient and more ergonomic.

## R4: Thread Safety Wrapper Design

**Decision**: Separate `ThreadSafeLruCache<K,V>` class wrapping
`LruCache<K,V>` with `mutable std::mutex` + `std::lock_guard`.

**Rationale**: Per clarification, the base `LruCache` MUST NOT contain
locking. Most callers are single-threaded LiteBus actors where mutex
overhead is waste. The wrapper simply acquires the lock before
delegating to the base class.

**Alternatives considered**:
- **Reader-writer lock (`std::shared_mutex`)**: `Get` promotes entries
  (mutates list), so read-lock is insufficient for most operations.
  Only `Contains`, `Peek`, `Size`, `Capacity` are truly read-only.
  The complexity is not justified for initial version.
- **Template policy**: More flexible but adds template parameter
  complexity; YAGNI for current use cases.

## R5: Move-Only Value Type Support

**Decision**: Use `std::move` consistently for value insertion and
eviction. `std::list` nodes have stable addresses, so no reallocation
concerns.

**Rationale**: FR-012 requires move-only type support (e.g.,
`std::unique_ptr<SnapshotData>`). `std::list::emplace_front` +
`std::move` on the value handles this correctly.

**Key implementation note**: `Get()` returns
`std::optional<std::reference_wrapper<V>>` (reference, not copy) to
avoid moving the value out of the cache on read. Only eviction and
`Remove` move the value.

## R6: Exception Safety in Eviction Callback

**Decision**: Catch exceptions from eviction callback, ensure entry is
still removed from internal data structures regardless.

**Rationale**: Edge case spec requires "cache MUST NOT leave internal
state inconsistent" if callback throws. Implementation wraps callback
invocation in try-catch, removes entry from map and list before or
regardless of callback outcome.

**Pattern**:
```cpp
void Evict() {
    auto node = std::move(member_list.back());
    member_list.pop_back();
    member_map.erase(node.first);
    if (member_onEvict) {
        try {
            member_onEvict(node.first, std::move(node.second));
        } catch (...) {
            // Log warning; entry already removed
        }
    }
}
```

## R7: CMake Integration Pattern

**Decision**: INTERFACE library with `target_include_directories`.

**Rationale**: Header-only libraries in CMake use `add_library(lru INTERFACE)`
with `target_include_directories(lru INTERFACE ...)`. Consumers link via
`target_link_libraries(consumer PRIVATE lru)` and get include paths
automatically. This matches the project's existing pattern for template
utilities.

**Registration**: Add `add_subdirectory(lru)` to
`functionsystem/src/common/CMakeLists.txt`.
