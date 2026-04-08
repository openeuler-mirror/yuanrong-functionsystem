# LRU Cache 通用缓存模块 API 文档

## 概述

本模块提供一个通用的 LRU（Least Recently Used，最近最少使用）缓存实现，位于
`functionsystem/src/common/lru/`。模块包含两个类：

- **`LruCache<K, V>`** — 无锁基础版本，适用于单线程场景（如 LiteBus Actor）
- **`ThreadSafeLruCache<K, V>`** — 互斥锁包装版本，适用于多线程并发访问场景

所有操作的平均时间复杂度为 **O(1)**。

## 头文件引入

```cpp
#include "lru/lru_cache.h"              // LruCache<K, V>
#include "lru/thread_safe_lru_cache.h"  // ThreadSafeLruCache<K, V>
```

## CMake 集成

在你的模块 `CMakeLists.txt` 中添加：

```cmake
target_link_libraries(your_module PRIVATE lru)
```

---

## LruCache<K, V>

### 模板参数

| 参数 | 约束 | 说明 |
|------|------|------|
| `K` | 需支持 `std::hash<K>` 和 `operator==` | 缓存键类型 |
| `V` | 需支持移动构造，可以是 move-only 类型 | 缓存值类型 |

### 类型别名

```cpp
using EvictionCallback = std::function<void(const K&, V&&)>;
```

淘汰回调函数类型。当缓存容量满时插入新条目，最久未使用的条目会被淘汰，
回调函数接收被淘汰条目的 key（const 引用）和 value（右值引用，所有权转移给回调方）。

### 构造函数

```cpp
explicit LruCache(size_t capacity, EvictionCallback onEvict = nullptr);
```

| 参数 | 说明 |
|------|------|
| `capacity` | 缓存最大容量（条目数），必须 > 0，否则触发断言 |
| `onEvict` | 可选的淘汰回调函数，默认为 nullptr（不回调） |

**拷贝语义**：禁用拷贝构造和拷贝赋值。
**移动语义**：支持移动构造和移动赋值。

### API 方法

#### Put — 插入或更新条目

```cpp
bool Put(const K& key, V value);
```

插入新条目或更新已有条目的值，并将该条目提升为最近使用（MRU）。

| 参数 | 说明 |
|------|------|
| `key` | 缓存键 |
| `value` | 缓存值（通过移动语义传入） |

**返回值**：`true` 表示插入了新条目，`false` 表示更新了已有条目。

**淘汰行为**：当缓存已满且插入新键时，最久未使用的条目（LRU）会被淘汰。
如果设置了淘汰回调，回调会在条目移除后被调用。

```cpp
LruCache<std::string, int> cache(3);
cache.Put("a", 1);   // 返回 true（新插入）
cache.Put("b", 2);   // 返回 true
cache.Put("c", 3);   // 返回 true
cache.Put("a", 100); // 返回 false（更新已有键）
cache.Put("d", 4);   // 返回 true，淘汰 "b"（最久未使用）
```

---

#### Get — 获取条目（提升为最近使用）

```cpp
std::optional<std::reference_wrapper<V>> Get(const K& key);
```

根据 key 查找缓存条目。如果命中，将该条目提升为最近使用（MRU）。

**返回值**：命中时返回值的引用包装；未命中返回 `std::nullopt`。

```cpp
auto result = cache.Get("a");
if (result) {
    int& value = result->get();  // 获取引用
    // 使用 value...
}
```

> **注意**：返回的是引用，不会拷贝值。对于 move-only 类型（如 `std::unique_ptr`），
> 可以安全地通过引用访问而不影响所有权。

---

#### Contains — 检查键是否存在（不提升）

```cpp
bool Contains(const K& key) const;
```

检查指定 key 是否存在于缓存中。**不会改变淘汰顺序**。

```cpp
if (cache.Contains("a")) {
    // key "a" 存在，但其在淘汰队列中的位置不变
}
```

---

#### Peek — 读取值（不提升）

```cpp
std::optional<std::reference_wrapper<const V>> Peek(const K& key) const;
```

读取指定 key 的值但**不改变淘汰顺序**。适用于诊断、监控等不应影响缓存行为的场景。

**返回值**：命中时返回 const 引用包装；未命中返回 `std::nullopt`。

```cpp
auto peeked = cache.Peek("a");
if (peeked) {
    const int& value = peeked->get();
    // 只读访问，不影响淘汰顺序
}
```

---

#### Remove — 显式删除条目

```cpp
bool Remove(const K& key);
```

从缓存中删除指定 key 的条目。**不会触发淘汰回调**。

**返回值**：`true` 表示找到并删除，`false` 表示 key 不存在。

---

#### Clear — 清空所有条目

```cpp
void Clear();
```

移除缓存中的所有条目。**不会触发淘汰回调**。

---

#### Size — 当前条目数

```cpp
size_t Size() const;
```

返回缓存中当前存储的条目数量。

---

#### Capacity — 最大容量

```cpp
size_t Capacity() const;
```

返回缓存的最大容量（构造时设定，不可变）。

---

## ThreadSafeLruCache<K, V>

互斥锁包装版本，API 与 `LruCache` 相同，但所有操作通过 `std::mutex` 保护。

### 与 LruCache 的区别

| 特性 | LruCache | ThreadSafeLruCache |
|------|----------|-------------------|
| 线程安全 | 否 | 是（互斥锁） |
| Get 返回类型 | `optional<reference_wrapper<V>>` | `optional<V>`（返回拷贝） |
| Peek 返回类型 | `optional<reference_wrapper<const V>>` | `optional<V>`（返回拷贝） |
| 移动语义 | 支持 | 不支持（mutex 不可移动） |
| 适用场景 | LiteBus Actor 内部 | 多线程共享访问 |

> **重要**：`ThreadSafeLruCache` 的 `Get` 和 `Peek` 返回值的**拷贝**而非引用，
> 以避免在持有引用期间其他线程修改或淘汰该条目导致悬空引用。

### 构造函数

```cpp
explicit ThreadSafeLruCache(size_t capacity, EvictionCallback onEvict = nullptr);
```

参数含义同 `LruCache`。

### 使用示例

```cpp
ThreadSafeLruCache<std::string, RouteInfo> routeCache(500);

// 所有操作线程安全
routeCache.Put("instance-001", RouteInfo{...});

auto route = routeCache.Get("instance-001");
if (route) {
    // route.value() 是 RouteInfo 的拷贝，可安全使用
}
```

---

## 使用场景示例

### 1. 快照存储缓存

当快照数据不再被活跃使用时，加入 LRU 队列；容量满时通过淘汰回调异步清理：

```cpp
LruCache<std::string, std::unique_ptr<SnapshotData>> snapshotCache(
    1000,
    [](const std::string& snapshotId, std::unique_ptr<SnapshotData>&& data) {
        // 异步回调：将快照刷写到持久化存储
        FlushToPersistentStorage(snapshotId, std::move(data));
    }
);

// 访问快照 — 提升为最近使用，不会被淘汰
auto snap = snapshotCache.Get("snap-001");

// 新增快照，若容量满则最久未使用的快照被淘汰，触发回调
snapshotCache.Put("snap-new", std::make_unique<SnapshotData>(...));
```

### 2. 实例路由信息缓存

```cpp
struct RouteInfo {
    std::string nodeAddress;
    uint16_t port;
    uint64_t version;
};

LruCache<std::string, RouteInfo> instanceRouteCache(
    2000,
    [](const std::string& instanceId, RouteInfo&& route) {
        LOG_INFO("路由淘汰: instance={}, node={}", instanceId, route.nodeAddress);
    }
);

// 非提升查询 — 用于诊断，不影响淘汰顺序
if (instanceRouteCache.Contains("instance-xyz")) {
    auto route = instanceRouteCache.Peek("instance-xyz");
    // 只读查看，不改变缓存行为
}
```

### 3. 在 LiteBus Actor 中使用

```cpp
class MyActor : public litebus::ActorBase {
protected:
    void Init() override {
        // Actor 内部使用无锁版本（Actor 消息循环是单线程的）
        member_cache = std::make_unique<LruCache<std::string, DataObj>>(
            500,
            [this](const std::string& key, DataObj&& data) {
                // 注意：在 Actor 回调中应使用 Defer 模式
                // 此处简化展示
                HandleEviction(key, std::move(data));
            }
        );
    }

private:
    std::unique_ptr<LruCache<std::string, DataObj>> member_cache;
};
```

---

## 淘汰回调说明

### 触发条件

| 操作 | 是否触发回调 |
|------|-------------|
| `Put`（容量满时插入新键） | 是 |
| `Put`（更新已有键） | 否 |
| `Remove` | 否 |
| `Clear` | 否 |

### 异常安全

淘汰回调中抛出的异常会被捕获并忽略。被淘汰的条目**已经**从缓存内部数据结构中
移除，不会因回调异常导致缓存状态不一致。

### 值的所有权

回调接收 `V&&`（右值引用），被淘汰的值的所有权完整转移给回调方。
对于 `std::unique_ptr` 等 move-only 类型，回调方可以通过 `std::move` 接管资源。

---

## 性能特征

| 操作 | 时间复杂度 | 说明 |
|------|-----------|------|
| Put | O(1) 均摊 | `unordered_map` 插入 + `list` 前插 |
| Get | O(1) 均摊 | `unordered_map` 查找 + `list::splice` |
| Contains | O(1) 均摊 | `unordered_map::count` |
| Peek | O(1) 均摊 | `unordered_map::find` |
| Remove | O(1) 均摊 | `unordered_map` 删除 + `list::erase` |
| Clear | O(n) | 清空所有条目 |
| Size | O(1) | `unordered_map::size` |
| Capacity | O(1) | 返回成员变量 |

**空间复杂度**：O(n)，其中 n 为缓存容量。每个条目在 `std::list` 和
`std::unordered_map` 中各存储一份引用。
