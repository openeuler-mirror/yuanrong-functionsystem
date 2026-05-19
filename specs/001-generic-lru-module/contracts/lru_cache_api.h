// API Contract: LruCache<K, V> and ThreadSafeLruCache<K, V>
// This file defines the public interface contract for the LRU cache module.
// Implementation details are omitted; see plan.md for design decisions.
//
// Location: functionsystem/src/common/lru/
// Feature: 001-generic-lru-module

#ifndef FUNCTIONSYSTEM_COMMON_LRU_LRU_CACHE_API_H
#define FUNCTIONSYSTEM_COMMON_LRU_LRU_CACHE_API_H

#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace functionsystem {

/// @brief Generic LRU (Least Recently Used) cache with O(1) operations.
///
/// Thread safety: This class is NOT thread-safe. For multi-threaded use,
/// see ThreadSafeLruCache<K, V>.
///
/// @tparam K Key type. Must be hashable (std::hash<K>) and equality-comparable.
/// @tparam V Value type. Must be move-constructible. May be move-only.
template <typename K, typename V>
class LruCache {
public:
    using EvictionCallback = std::function<void(const K&, V&&)>;

    /// @brief Construct an LRU cache with the given capacity.
    /// @param capacity Maximum number of entries. Must be > 0.
    /// @param onEvict Optional callback invoked when an entry is evicted.
    ///   The value is moved to the callback for ownership transfer.
    explicit LruCache(size_t capacity, EvictionCallback onEvict = nullptr);

    // Non-copyable, movable
    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;
    LruCache(LruCache&&) noexcept = default;
    LruCache& operator=(LruCache&&) noexcept = default;

    ~LruCache() = default;

    /// @brief Insert or update an entry, promoting it to MRU.
    /// @param key The key to insert/update.
    /// @param value The value to store (moved in).
    /// @return true if a new entry was inserted, false if an existing
    ///   entry was updated.
    /// @note If capacity is exceeded, the LRU entry is evicted first
    ///   (eviction callback invoked if set).
    bool Put(const K& key, V value);

    /// @brief Retrieve a value by key, promoting the entry to MRU.
    /// @param key The key to look up.
    /// @return Reference to the value if found, std::nullopt on miss.
    std::optional<std::reference_wrapper<V>> Get(const K& key);

    /// @brief Check if a key exists WITHOUT promoting in eviction order.
    /// @param key The key to check.
    /// @return true if the key is present in the cache.
    bool Contains(const K& key) const;

    /// @brief Read a value WITHOUT promoting in eviction order.
    /// @param key The key to peek.
    /// @return Const reference to the value if found, std::nullopt on miss.
    std::optional<std::reference_wrapper<const V>> Peek(const K& key) const;

    /// @brief Remove a specific entry from the cache.
    /// @param key The key to remove.
    /// @return true if the entry was found and removed, false if not found.
    /// @note Eviction callback is NOT invoked for explicit removal.
    bool Remove(const K& key);

    /// @brief Remove all entries from the cache.
    /// @note Eviction callback is NOT invoked for Clear.
    void Clear();

    /// @brief Return the current number of entries.
    size_t Size() const;

    /// @brief Return the maximum capacity.
    size_t Capacity() const;
};

/// @brief Thread-safe wrapper around LruCache<K, V>.
///
/// All operations are guarded by a mutex. For single-threaded contexts
/// (e.g., LiteBus actors), prefer using LruCache directly to avoid
/// lock overhead.
///
/// @tparam K Key type (same constraints as LruCache).
/// @tparam V Value type (same constraints as LruCache).
template <typename K, typename V>
class ThreadSafeLruCache {
public:
    using EvictionCallback = typename LruCache<K, V>::EvictionCallback;

    /// @brief Construct a thread-safe LRU cache.
    /// @param capacity Maximum number of entries. Must be > 0.
    /// @param onEvict Optional eviction callback.
    explicit ThreadSafeLruCache(size_t capacity,
                                EvictionCallback onEvict = nullptr);

    // Non-copyable, non-movable (mutex is not movable)
    ThreadSafeLruCache(const ThreadSafeLruCache&) = delete;
    ThreadSafeLruCache& operator=(const ThreadSafeLruCache&) = delete;
    ThreadSafeLruCache(ThreadSafeLruCache&&) = delete;
    ThreadSafeLruCache& operator=(ThreadSafeLruCache&&) = delete;

    ~ThreadSafeLruCache() = default;

    /// @brief Thread-safe Put. See LruCache::Put.
    bool Put(const K& key, V value);

    /// @brief Thread-safe Get. See LruCache::Get.
    /// @note Returns a copy of the value (not a reference) for safety.
    std::optional<V> Get(const K& key);

    /// @brief Thread-safe Contains. See LruCache::Contains.
    bool Contains(const K& key) const;

    /// @brief Thread-safe Peek. See LruCache::Peek.
    /// @note Returns a copy of the value (not a reference) for safety.
    std::optional<V> Peek(const K& key) const;

    /// @brief Thread-safe Remove. See LruCache::Remove.
    bool Remove(const K& key);

    /// @brief Thread-safe Clear. See LruCache::Clear.
    void Clear();

    /// @brief Thread-safe Size. See LruCache::Size.
    size_t Size() const;

    /// @brief Thread-safe Capacity. See LruCache::Capacity.
    size_t Capacity() const;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_COMMON_LRU_LRU_CACHE_API_H
