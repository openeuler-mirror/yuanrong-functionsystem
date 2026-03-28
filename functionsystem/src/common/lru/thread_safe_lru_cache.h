/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FUNCTIONSYSTEM_COMMON_LRU_THREAD_SAFE_LRU_CACHE_H
#define FUNCTIONSYSTEM_COMMON_LRU_THREAD_SAFE_LRU_CACHE_H

#include <mutex>
#include <optional>

#include "lru/lru_cache.h"

namespace functionsystem {

template <typename K, typename V>
class ThreadSafeLruCache {
public:
    using EvictionCallback = typename LruCache<K, V>::EvictionCallback;

    explicit ThreadSafeLruCache(size_t capacity,
                                EvictionCallback onEvict = nullptr)
        : member_cache(capacity, std::move(onEvict))
    {
    }

    ThreadSafeLruCache(const ThreadSafeLruCache&) = delete;
    ThreadSafeLruCache& operator=(const ThreadSafeLruCache&) = delete;
    ThreadSafeLruCache(ThreadSafeLruCache&&) = delete;
    ThreadSafeLruCache& operator=(ThreadSafeLruCache&&) = delete;

    ~ThreadSafeLruCache() = default;

    bool Put(const K& key, V value)
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        return member_cache.Put(key, std::move(value));
    }

    std::optional<V> Get(const K& key)
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        auto result = member_cache.Get(key);
        if (result) {
            return result->get();
        }
        return std::nullopt;
    }

    bool Contains(const K& key) const
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        return member_cache.Contains(key);
    }

    std::optional<V> Peek(const K& key) const
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        auto result = member_cache.Peek(key);
        if (result) {
            return result->get();
        }
        return std::nullopt;
    }

    bool Remove(const K& key)
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        return member_cache.Remove(key);
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        member_cache.Clear();
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        return member_cache.Size();
    }

    size_t Capacity() const
    {
        std::lock_guard<std::mutex> lock(member_mutex);
        return member_cache.Capacity();
    }

private:
    LruCache<K, V> member_cache;
    mutable std::mutex member_mutex;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_COMMON_LRU_THREAD_SAFE_LRU_CACHE_H
