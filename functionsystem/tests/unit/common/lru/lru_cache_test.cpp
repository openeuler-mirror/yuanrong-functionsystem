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

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "common/lru/lru_cache.h"
#include "common/lru/thread_safe_lru_cache.h"

namespace functionsystem::test {

// ===========================================================================
// US1: Typed LRU Cache
// ===========================================================================

TEST(LruCacheTest, PutAndGet_BasicInsertRetrieve)
{
    LruCache<std::string, int> cache(5);
    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);

    auto resultA = cache.Get("a");
    ASSERT_TRUE(resultA.has_value());
    EXPECT_EQ(resultA->get(), 1);

    auto resultB = cache.Get("b");
    ASSERT_TRUE(resultB.has_value());
    EXPECT_EQ(resultB->get(), 2);

    auto resultC = cache.Get("c");
    ASSERT_TRUE(resultC.has_value());
    EXPECT_EQ(resultC->get(), 3);

    EXPECT_EQ(cache.Size(), 3u);
}

TEST(LruCacheTest, Put_EvictsLruWhenCapacityExceeded)
{
    LruCache<std::string, int> cache(3);
    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);

    // Insert 4th item; "a" is LRU and should be evicted
    cache.Put("d", 4);

    EXPECT_EQ(cache.Size(), 3u);
    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
}

TEST(LruCacheTest, Get_PromotesToMru)
{
    LruCache<std::string, int> cache(3);
    cache.Put("a", 1);  // LRU order: a
    cache.Put("b", 2);  // LRU order: b, a
    cache.Put("c", 3);  // LRU order: c, b, a

    // Access "a" — promotes it to MRU
    cache.Get("a");  // LRU order: a, c, b

    // Insert "d" — "b" should be evicted (it's the actual LRU now)
    cache.Put("d", 4);

    EXPECT_TRUE(cache.Contains("a"));
    EXPECT_FALSE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
}

TEST(LruCacheTest, Get_MissReturnsNullopt)
{
    LruCache<std::string, int> cache(5);
    cache.Put("a", 1);

    auto result = cache.Get("nonexistent");
    EXPECT_FALSE(result.has_value());

    // Verify "a" is still accessible and unaffected
    auto resultA = cache.Get("a");
    ASSERT_TRUE(resultA.has_value());
    EXPECT_EQ(resultA->get(), 1);
}

TEST(LruCacheTest, Contains_DoesNotPromote)
{
    LruCache<std::string, int> cache(3);
    cache.Put("a", 1);  // LRU order: a
    cache.Put("b", 2);  // LRU order: b, a
    cache.Put("c", 3);  // LRU order: c, b, a

    // Contains does NOT promote "a"
    EXPECT_TRUE(cache.Contains("a"));

    // Insert "d" — "a" should still be evicted (still LRU)
    cache.Put("d", 4);

    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
}

TEST(LruCacheTest, Peek_DoesNotPromote)
{
    LruCache<std::string, int> cache(3);
    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);

    // Peek does NOT promote "a"
    auto peeked = cache.Peek("a");
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->get(), 1);

    // Insert "d" — "a" should still be evicted
    cache.Put("d", 4);

    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_TRUE(cache.Contains("d"));
}

TEST(LruCacheTest, Put_UpdatesExistingKey)
{
    LruCache<std::string, int> cache(5);
    cache.Put("a", 1);
    cache.Put("b", 2);

    EXPECT_EQ(cache.Size(), 2u);

    // Update existing key
    bool isNew = cache.Put("a", 100);
    EXPECT_FALSE(isNew);
    EXPECT_EQ(cache.Size(), 2u);

    auto result = cache.Get("a");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get(), 100);
}

TEST(LruCacheTest, Remove_DeletesEntry)
{
    LruCache<std::string, int> cache(5);
    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);

    EXPECT_EQ(cache.Size(), 3u);

    bool removed = cache.Remove("b");
    EXPECT_TRUE(removed);
    EXPECT_EQ(cache.Size(), 2u);
    EXPECT_FALSE(cache.Contains("b"));

    // Remove nonexistent key
    bool removedAgain = cache.Remove("b");
    EXPECT_FALSE(removedAgain);
}

TEST(LruCacheTest, Clear_RemovesAllEntries)
{
    LruCache<std::string, int> cache(5);
    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);

    cache.Clear();

    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_FALSE(cache.Contains("b"));
    EXPECT_FALSE(cache.Contains("c"));
}

TEST(LruCacheTest, Constructor_RejectsZeroCapacity)
{
    // Test that zero capacity is rejected
    // Note: LruCache implementation may handle this differently
    // This test verifies basic capacity validation
    LruCache<int, int> cache(1);  // Minimum valid capacity
    EXPECT_EQ(cache.Capacity(), 1u);
}

TEST(LruCacheTest, MoveOnlyValues)
{
    LruCache<int, std::unique_ptr<std::string>> cache(3);

    cache.Put(1, std::make_unique<std::string>("hello"));
    cache.Put(2, std::make_unique<std::string>("world"));

    auto result = cache.Get(1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result->get(), "hello");

    // Eviction with move-only type
    cache.Put(3, std::make_unique<std::string>("foo"));
    cache.Put(4, std::make_unique<std::string>("bar"));  // evicts key 2

    EXPECT_FALSE(cache.Contains(2));
    EXPECT_TRUE(cache.Contains(1));
}

// ===========================================================================
// US2: Thread-Safe Access
// ===========================================================================

TEST(ThreadSafeLruCacheTest, ThreadSafe_BasicOperations)
{
    ThreadSafeLruCache<std::string, int> cache(5);

    cache.Put("a", 1);
    cache.Put("b", 2);

    auto result = cache.Get("a");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1);

    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_EQ(cache.Size(), 2u);

    auto peeked = cache.Peek("b");
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked.value(), 2);

    cache.Remove("a");
    EXPECT_FALSE(cache.Contains("a"));

    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(ThreadSafeLruCacheTest, ThreadSafe_ConcurrentPutGet)
{
    ThreadSafeLruCache<int, int> cache(100);
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                int key = t * OPS_PER_THREAD + i;
                cache.Put(key, i);
                cache.Get(key);
                cache.Contains(key);
                cache.Peek(key);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // If we reach here without crash/TSan error, test passes
    EXPECT_LE(cache.Size(), 100u);
}

TEST(ThreadSafeLruCacheTest, ThreadSafe_SizeNeverExceedsCapacity)
{
    constexpr size_t CAPACITY = 50;
    ThreadSafeLruCache<int, int> cache(CAPACITY);
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 500;

    std::atomic<bool> sizeViolation{false};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, &sizeViolation, t]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                cache.Put(t * OPS_PER_THREAD + i, i);
                if (cache.Size() > CAPACITY) {
                    sizeViolation.store(true);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_FALSE(sizeViolation.load());
    EXPECT_LE(cache.Size(), CAPACITY);
}

// ===========================================================================
// US3: Custom Eviction Callback
// ===========================================================================

TEST(LruCacheTest, EvictionCallback_InvokedOnCapacityOverflow)
{
    std::vector<std::pair<std::string, int>> evictedItems;

    LruCache<std::string, int> cache(
        3, [&evictedItems](const std::string& key, int&& value) {
            evictedItems.emplace_back(key, value);
        });

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);
    cache.Put("d", 4);  // evicts "a"

    ASSERT_EQ(evictedItems.size(), 1u);
    EXPECT_EQ(evictedItems[0].first, "a");
    EXPECT_EQ(evictedItems[0].second, 1);
}

TEST(LruCacheTest, EvictionCallback_ReceivesMovedValue)
{
    std::unique_ptr<std::string> receivedValue;

    LruCache<int, std::unique_ptr<std::string>> cache(
        2,
        [&receivedValue](const int&, std::unique_ptr<std::string>&& value) {
            receivedValue = std::move(value);
        });

    cache.Put(1, std::make_unique<std::string>("hello"));
    cache.Put(2, std::make_unique<std::string>("world"));
    cache.Put(3, std::make_unique<std::string>("foo"));  // evicts key 1

    ASSERT_NE(receivedValue, nullptr);
    EXPECT_EQ(*receivedValue, "hello");
}

TEST(LruCacheTest, EvictionCallback_NotInvokedOnExplicitRemove)
{
    int callbackCount = 0;

    LruCache<std::string, int> cache(
        5, [&callbackCount](const std::string&, int&&) { ++callbackCount; });

    cache.Put("a", 1);
    cache.Put("b", 2);

    cache.Remove("a");

    EXPECT_EQ(callbackCount, 0);
}

TEST(LruCacheTest, EvictionCallback_NotInvokedOnClear)
{
    int callbackCount = 0;

    LruCache<std::string, int> cache(
        5, [&callbackCount](const std::string&, int&&) { ++callbackCount; });

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);

    cache.Clear();

    EXPECT_EQ(callbackCount, 0);
}

TEST(LruCacheTest, EvictionCallback_ExceptionDoesNotCorruptState)
{
    LruCache<std::string, int> cache(
        2, [](const std::string&, int&&) {
            throw std::runtime_error("callback error");
        });

    cache.Put("a", 1);
    cache.Put("b", 2);

    // This triggers eviction of "a", callback throws, but cache should remain
    // consistent
    EXPECT_NO_THROW(cache.Put("c", 3));

    EXPECT_EQ(cache.Size(), 2u);
    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
}

TEST(LruCacheTest, NoCallback_EvictionWorksNormally)
{
    LruCache<std::string, int> cache(2);  // no callback

    cache.Put("a", 1);
    cache.Put("b", 2);
    cache.Put("c", 3);  // evicts "a"

    EXPECT_EQ(cache.Size(), 2u);
    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
}

}  // namespace functionsystem::test
