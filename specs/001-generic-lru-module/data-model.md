# Data Model: Generic LRU Cache Module

**Date**: 2026-02-15
**Feature**: 001-generic-lru-module

## Entities

### LruCache<K, V>

Core template class implementing a bounded LRU eviction cache.

**Template Parameters**:
- `K` — Key type. MUST be hashable (`std::hash<K>`) and equality-comparable (`operator==`).
- `V` — Value type. MUST be move-constructible. MAY be move-only (e.g., `std::unique_ptr<T>`).

**Members** (private):

| Name | Type | Description |
|------|------|-------------|
| `member_capacity` | `size_t` | Maximum number of entries (immutable after construction) |
| `member_list` | `std::list<std::pair<K, V>>` | Doubly-linked list; front = MRU, back = LRU |
| `member_map` | `std::unordered_map<K, ListIterator>` | Key → iterator into `member_list` for O(1) lookup |
| `member_onEvict` | `EvictionCallback` | Optional callback invoked on eviction (nullable) |

**Type Aliases**:

| Alias | Expansion |
|-------|-----------|
| `ListType` | `std::list<std::pair<K, V>>` |
| `ListIterator` | `typename ListType::iterator` |
| `EvictionCallback` | `std::function<void(const K&, V&&)>` |

**Invariants**:
- `member_map.size() == member_list.size()` (always)
- `member_list.size() <= member_capacity` (always)
- `member_capacity > 0` (enforced at construction)
- For every `(k, it)` in `member_map`: `it->first == k`

### ThreadSafeLruCache<K, V>

Mutex-guarded wrapper around `LruCache<K, V>`.

**Members** (private):

| Name | Type | Description |
|------|------|-------------|
| `member_cache` | `LruCache<K, V>` | Underlying cache instance |
| `member_mutex` | `mutable std::mutex` | Guards all operations on `member_cache` |

**Invariants**:
- All public method calls acquire `member_mutex` before delegating
- No public method holds the mutex while invoking the eviction callback
  (callback is invoked while lock is held — acceptable for initial
  version; callers SHOULD keep callbacks fast or defer async work)

## State Transitions

### Cache Entry Lifecycle

```text
[Not in cache]
    │
    ▼  Put(key, value)
[In cache, MRU position]
    │
    ├── Get(key) → [Promoted to MRU]
    ├── Put(key, newValue) → [Updated, promoted to MRU]
    ├── Peek(key) → [No position change]
    ├── Contains(key) → [No position change]
    │
    ▼  (capacity exceeded on new Put, this entry is LRU)
[Eviction callback invoked with (key, std::move(value))]
    │
    ▼
[Removed from cache]
```

### Explicit Removal

```text
[In cache] → Remove(key) → [Removed from cache]
[In cache] → Clear() → [All entries removed]
```

## Relationships

```text
ThreadSafeLruCache<K,V> ──owns──▶ LruCache<K,V>
                         ──owns──▶ std::mutex

LruCache<K,V> ──owns──▶ std::list<pair<K,V>>     (ordered entries)
              ──owns──▶ std::unordered_map<K,iter> (index)
              ──owns──▶ EvictionCallback            (optional)
```

## Example Instantiations

| Use Case | K | V | Notes |
|----------|---|---|-------|
| Snapshot storage | `std::string` (snapshot ID) | `SnapshotData` (or `unique_ptr<SnapshotData>`) | Eviction callback flushes to persistent storage |
| Instance routing | `std::string` (instance ID) | `RouteInfo` struct | Used by `instance_view` for route caching |
| Generic test | `int` | `std::string` | Unit test instantiation |
