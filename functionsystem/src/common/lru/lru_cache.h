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

#ifndef FUNCTIONSYSTEM_COMMON_LRU_LRU_CACHE_H
#define FUNCTIONSYSTEM_COMMON_LRU_LRU_CACHE_H

#include <cassert>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace functionsystem {

template <typename K, typename V>
class LruCache {
public:
    using EvictionCallback = std::function<void(const K&, V&&)>;

    explicit LruCache(size_t capacity, EvictionCallback onEvict = nullptr)
        : member_capacity(capacity), member_onEvict(std::move(onEvict))
    {
        assert(capacity > 0 && "LruCache capacity must be greater than 0");
    }

    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;
    LruCache(LruCache&&) noexcept = default;
    LruCache& operator=(LruCache&&) noexcept = default;

    ~LruCache() = default;

    bool Put(const K& key, V value)
    {
        auto it = member_map.find(key);
        if (it != member_map.end()) {
            it->second->second = std::move(value);
            member_list.splice(member_list.begin(), member_list, it->second);
            return false;
        }

        if (member_list.size() >= member_capacity) {
            EvictLru();
        }

        member_list.emplace_front(key, std::move(value));
        member_map[key] = member_list.begin();
        return true;
    }

    std::optional<std::reference_wrapper<V>> Get(const K& key)
    {
        auto it = member_map.find(key);
        if (it == member_map.end()) {
            return std::nullopt;
        }
        member_list.splice(member_list.begin(), member_list, it->second);
        return std::ref(it->second->second);
    }

    bool Contains(const K& key) const
    {
        return member_map.count(key) > 0;
    }

    std::optional<std::reference_wrapper<const V>> Peek(const K& key) const
    {
        auto it = member_map.find(key);
        if (it == member_map.end()) {
            return std::nullopt;
        }
        return std::cref(it->second->second);
    }

    bool Remove(const K& key)
    {
        auto it = member_map.find(key);
        if (it == member_map.end()) {
            return false;
        }
        member_list.erase(it->second);
        member_map.erase(it);
        return true;
    }

    void Clear()
    {
        member_list.clear();
        member_map.clear();
    }

    size_t Size() const { return member_map.size(); }

    size_t Capacity() const { return member_capacity; }

private:
    using ListType = std::list<std::pair<K, V>>;
    using ListIterator = typename ListType::iterator;

    void EvictLru()
    {
        auto& back = member_list.back();
        K evictedKey = back.first;
        V evictedValue = std::move(back.second);
        member_map.erase(evictedKey);
        member_list.pop_back();

        if (member_onEvict) {
            try {
                member_onEvict(evictedKey, std::move(evictedValue));
            } catch (...) {
                // Swallow exception to maintain cache consistency.
                // Entry is already removed from internal data structures.
            }
        }
    }

    size_t member_capacity;
    ListType member_list;
    std::unordered_map<K, ListIterator> member_map;
    EvictionCallback member_onEvict;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_COMMON_LRU_LRU_CACHE_H
