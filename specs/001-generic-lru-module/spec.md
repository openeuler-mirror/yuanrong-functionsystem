# Feature Specification: Generic LRU Cache Module

**Feature Branch**: `001-generic-lru-module`
**Created**: 2026-02-15
**Status**: Draft
**Input**: User description: "在functionsystem/src/common下提供一个通用的LRU模块，支持不同类型对象的LRU（比如快照存储的LRU，实例路由信息的LRU）"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Create a Typed LRU Cache (Priority: P1)

As a C++ developer in the FunctionSystem project, I need a generic
(template-based) LRU cache that I can instantiate with any key-value
type pair, so that I can cache different kinds of objects (snapshots,
instance routing info, etc.) without writing separate cache
implementations for each.

**Why this priority**: This is the core value of the feature. Without a
type-generic LRU, every module must implement its own caching logic,
leading to duplicated code and inconsistent eviction behavior.

**Independent Test**: Can be fully tested by creating an
`LruCache<std::string, int>` instance, inserting items, and verifying
that the least-recently-used item is evicted when capacity is exceeded.

**Acceptance Scenarios**:

1. **Given** an LRU cache with capacity N, **When** N+1 distinct items
   are inserted, **Then** the least-recently-used item is evicted and
   the cache size remains N.
2. **Given** an LRU cache with items A, B, C (inserted in order),
   **When** item A is accessed (Get), then a new item D is inserted
   exceeding capacity, **Then** item B (the actual LRU) is evicted, not
   A.
3. **Given** an LRU cache, **When** Get is called for a non-existent
   key, **Then** a miss is indicated (e.g., returns false or nullopt)
   without modifying eviction order.

---

### User Story 2 - Thread-Safe Access (Priority: P2)

As a developer using the LRU cache in a multi-threaded LiteBus actor
environment, I need the cache to provide thread-safe operations, so that
concurrent reads and writes do not cause data races.

**Why this priority**: FunctionSystem components run as LiteBus actors
which may access shared caches. Thread safety is essential for
correctness in production.

**Independent Test**: Can be tested by launching multiple threads that
concurrently insert and read from the same LRU cache instance, verifying
no crashes or data corruption occur.

**Acceptance Scenarios**:

1. **Given** an LRU cache shared across multiple threads, **When**
   concurrent Put and Get operations are performed, **Then** no data
   races, crashes, or undefined behavior occur.
2. **Given** concurrent evictions triggered by multiple writers,
   **Then** the cache size never exceeds the configured capacity.

---

### User Story 3 - Cache with Custom Eviction Callback (Priority: P3)

As a developer, I need to register an optional callback that is invoked
when an item is evicted, so that I can perform cleanup (e.g., release
resources, log eviction events, persist data before removal).

**Why this priority**: Certain use cases like snapshot storage require
cleanup actions on eviction (e.g., flushing snapshot data to persistent
storage before removal from cache).

**Independent Test**: Can be tested by setting an eviction callback that
records evicted keys, then inserting items beyond capacity and verifying
the callback was invoked with the correct evicted key-value pair.

**Acceptance Scenarios**:

1. **Given** an LRU cache with an eviction callback registered,
   **When** an item is evicted due to capacity overflow, **Then** the
   callback is invoked with the evicted key and value before removal.
2. **Given** an LRU cache with no eviction callback, **When** an item
   is evicted, **Then** eviction proceeds normally without errors.

---

### Edge Cases

- What happens when capacity is set to 0? The cache MUST reject this at
  construction time (e.g., assert or return error).
- What happens when Put is called with a key that already exists? The
  value MUST be updated in-place and the item promoted to
  most-recently-used.
- What happens when the cache is used with move-only types? The cache
  MUST support move-only value types (e.g., `std::unique_ptr`).
- What happens when the eviction callback throws an exception? The cache
  MUST NOT leave internal state inconsistent; the item MUST still be
  removed.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The LRU module MUST be a C++ class template parameterized
  by key type and value type (`LruCache<K, V>`).
- **FR-002**: The module MUST support `Put(key, value)` to insert or
  update an entry, promoting it to most-recently-used.
- **FR-003**: The module MUST support `Get(key)` to retrieve an entry
  and promote it to most-recently-used; MUST indicate cache miss for
  absent keys.
- **FR-004**: The module MUST support `Contains(key)` to check existence
  without promoting the entry in eviction order.
- **FR-005**: The module MUST support `Peek(key)` to read a value without
  promoting the entry in eviction order; MUST indicate cache miss for
  absent keys.
- **FR-006**: The module MUST support `Remove(key)` to explicitly delete
  a single entry.
- **FR-007**: The module MUST support `Clear()` to remove all entries.
- **FR-008**: The module MUST support `Size()` and `Capacity()` queries.
- **FR-009**: The module MUST automatically evict the
  least-recently-used entry when capacity is exceeded during Put.
- **FR-010**: The module MUST support an optional eviction callback
  invoked with the evicted key-value pair.
- **FR-011**: The module MUST provide a separate `ThreadSafeLruCache<K,V>`
  wrapper class that adds mutex protection around the base `LruCache`.
  The base `LruCache` itself MUST NOT contain any locking, so that
  single-threaded callers (e.g., LiteBus actors) avoid lock overhead.
- **FR-012**: The module MUST support move-only value types.
- **FR-013**: The module MUST reside under
  `functionsystem/src/common/lru/`.
- **FR-014**: The module MUST follow the project C++ naming conventions
  (small camelCase variables, `member_` prefix for members, ALL_CAPS
  constants).

### Key Entities

- **LruCache<K, V>**: The core template class representing a
  bounded-capacity cache with LRU eviction policy. Key attributes:
  capacity (max entries), current size, eviction callback.
- **Cache Entry**: An individual key-value pair stored in the cache,
  with metadata for recency tracking (position in access-order list).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Developers can instantiate the LRU cache with any
  key-value type pair and perform Get/Put/Remove operations correctly.
- **SC-002**: The cache correctly evicts the least-recently-used item
  when capacity is exceeded, verified by unit tests covering at least 10
  distinct eviction scenarios.
- **SC-003**: Concurrent access from multiple threads produces no data
  races (verified via thread sanitizer or equivalent).
- **SC-004**: All public API methods complete in O(1) average time
  complexity per operation.
- **SC-005**: The module is reusable across at least two distinct use
  cases (snapshot storage, instance routing info) without modification.

## Clarifications

### Session 2026-02-15

- Q: Thread safety architecture — single class with built-in mutex, separate wrapper, or template policy? → A: Separate `ThreadSafeLruCache<K,V>` wrapper; base class stays lock-free.
- Q: Should the cache expose hit/miss statistics for observability? → A: No built-in statistics; callers track externally if needed.
- Q: Should the cache provide non-promoting existence check / peek? → A: Yes, add both `Contains(key)` and `Peek(key)` methods.

## Assumptions

- The LRU cache is an in-memory data structure; persistence is not in
  scope (callers handle persistence via eviction callbacks if needed).
- Capacity is measured by entry count, not byte size. If byte-based
  capacity is needed, it can be added in a future iteration.
- The project uses C++17 or later, so `std::optional`, structured
  bindings, and `if constexpr` are available.
- Thread safety is provided via `std::mutex`; lock-free
  implementations are out of scope for the initial version.
