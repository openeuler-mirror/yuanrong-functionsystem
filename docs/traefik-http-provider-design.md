# Traefik HTTP Provider Design

## 背景与动机

FunctionMaster 管理大量函数实例，每个实例通过 `portForward` 扩展字段暴露端口。Traefik 作为反向代理需要动态感知这些实例的路由信息。

### 现有方案的不足

原有 Traefik etcd registry 方案（`enable_traefik_registry`）将路由信息写入 etcd，Traefik 通过 etcd provider 读取。存在以下问题：

1. **额外依赖**：要求 Traefik 能直接访问 etcd，增加了网络拓扑约束
2. **写放大**：每次实例状态变更都需写 etcd，在大规模场景下增加 etcd 负载
3. **TTL 续约开销**：路由条目依赖 lease TTL 维持活性，产生持续的续约写入

### HTTP Provider 方案

Traefik 原生支持 [HTTP Provider](https://doc.traefik.io/traefik/providers/http/)——定期轮询 HTTP 端点获取动态配置。FunctionMaster 只需暴露一个 `GET /traefik/config` 端点，Traefik 即可拉取完整路由表。

**优势**：
- 无 etcd 依赖，Traefik 只需能访问 FunctionMaster HTTP 端口
- 只读拉取，不增加 MetaStore 写负载
- 路由信息与实例生命周期天然绑定，无需额外 TTL 机制

## 架构总览

```
┌─────────┐ poll GET /traefik/config  ┌──────────────────────────────────┐
│ Traefik │ ◄────────────────────────  │       FunctionMaster (Leader)    │
└─────────┘                            │                                  │
                                       │  ┌────────────────────────────┐  │
                                       │  │   TraefikApiRouterRegister │  │
                                       │  │   (HTTP handler)           │  │
                                       │  └─────────┬──────────────────┘  │
                                       │            │                     │
                                       │  ┌─────────▼──────────────────┐  │
                                       │  │   TraefikRouteCache        │  │
                                       │  │   (sorted-key JSON cache)  │  │
                                       │  └─────────▲──────────────────┘  │
                                       │            │                     │
                                       │  ┌─────────┴──────────────────┐  │
                                       │  │  InstanceManagerActor      │  │
                                       │  │  (MetaStore watch events)  │  │
                                       │  └────────────────────────────┘  │
                                       └──────────────────────────────────┘
```

### 组件职责

| 组件 | 职责 |
|------|------|
| `TraefikRouteCache` | 维护实例路由表，生成确定性 JSON 配置 |
| `TraefikApiRouterRegister` | HTTP handler，注册 `/traefik/config` 端点 |
| `TraefikLeaderContext` | 线程安全的 Leader 状态共享 |
| `InstanceManagerActor` | 监听 MetaStore watch 事件，驱动路由表更新 |
| `GlobalSchedDriver` | 初始化和组装上述组件 |

## 核心设计

### 1. 路由表 (TraefikRouteCache)

#### 数据结构

```cpp
struct RouteEntry {
    std::string routerName;    // 清洗后的 ID + "-p" + sandboxPort
    std::string backendURL;    // "https://10.0.0.1:40001" 或 "http://..."
    std::string sandboxPort;   // 容器内端口 (用于路由规则)
    bool        useHttps;      // 是否 HTTPS 后端
};

// routeTable_: unordered_map<instanceID, vector<RouteEntry>>
```

#### 端口映射解析

实例的 `extensions["portForward"]` 字段是一个 JSON 数组字符串，支持两种格式：

```
新格式 (3段): "protocol:hostPort:containerPort"  → "https:40001:8080"
旧格式 (2段): "hostPort:containerPort"           → "40001:8080" (默认 HTTP)
```

#### ID 清洗规则

Traefik 对 router/service 名称有字符限制，需要清洗 instanceID：
- `@` → `-at-`
- `/` `.` `_` → `-`
- 截断至 200 字符 (`MAX_ROUTER_NAME_LEN`)

最终命名：`routerName = sanitizeID(instanceID) + "-p" + sandboxPort`

### 2. 确定性 JSON 输出

**问题**：Traefik 用 FNV hash 比较前后两次 poll 的响应体。如果 JSON key 顺序不稳定，即使路由未变也会产生不同 hash，导致 Traefik 反复 reload。

**方案**：
- 使用 `nlohmann::json`（底层 `std::map`），所有 key 自动按字典序排列
- 路由条目按 `routerName` 排序后写入 JSON
- 结果：相同输入 → 逐字节相同的 JSON 输出

```cpp
// 排序确保确定性
std::map<std::string, const RouteEntry*> sortedRoutes;
for (const auto& [instanceID, entries] : routeTable_) {
    for (const auto& entry : entries) {
        sortedRoutes[entry.routerName] = &entry;
    }
}
```

#### 脏标记 + 缓存

- `dirty_`（`std::atomic<bool>`）：路由表变更时置 true
- `GetConfigJSON()`：dirty 时调用 `BuildConfigJSON()` 重建缓存，否则返回上次结果
- 轮询频率（默认 1s）远高于路由变更频率，缓存命中率极高

### 3. Traefik 兼容性：空 routers/services 处理

**问题**：Traefik 内部 parser (`file.DecodeContent`) 无法将空 JSON 对象 `{}` 解码为 Go map 类型，会报 "cannot be a standalone element" 错误。当所有实例退出后，如果返回 `"routers": {}`，Traefik 会拒绝整个配置，导致旧路由残留。

**方案**：当 `routers` 或 `services` map 为空时，在 JSON 中直接省略该 key。Traefik 对缺失 key 的语义是"无路由"。

```json
// 空路由表 → 只保留 middleware
{
  "http": {
    "middlewares": {
      "stripprefix-all": {
        "stripPrefixRegex": { "regex": ["^/[^/]+/[0-9]+"] }
      }
    }
  }
}

// 有路由时 → 完整结构
{
  "http": {
    "middlewares": { "stripprefix-all": { ... } },
    "routers": {
      "instance-a-p8080": {
        "entryPoints": ["websecure"],
        "middlewares": ["stripprefix-all"],
        "rule": "PathPrefix(`/instance-a/8080`)",
        "service": "instance-a-p8080",
        "tls": {}
      }
    },
    "services": {
      "instance-a-p8080": {
        "loadBalancer": {
          "servers": [{ "url": "https://10.0.0.1:40001" }],
          "serversTransport": "yr-backend-tls@file"
        }
      }
    }
  }
}
```

### 4. Standby-to-Leader 转发

在 Active-Standby 部署中，Traefik 通过 LoadBalancer 轮询可能命中 standby 节点。只有 Leader 拥有权威路由表。

详细设计见 [traefik-leader-forward-design.md](traefik-leader-forward-design.md)。

**核心机制**：

```
Traefik ──poll──► LB ──► FunctionMaster (standby)
                          │  isLeader=false
                          │  forward → leader
                          ▼
                        FunctionMaster (leader)
                          │  isLeader=true
                          │  serve from local cache
                          ▼
                        JSON response
```

**TraefikLeaderContext**：
- `isLeader`（`std::atomic<bool>`）：HTTP handler 热路径无锁检查
- `leaderHttpAddress`（`std::shared_mutex` 保护）：Leader 的 `ip:port`
- 通过 Explorer 的 `AddLeaderChangedCallback` 更新

**关键决策——为什么返回 503 而不是空 JSON**：

Traefik 对非 200 响应视为网络错误，保留 last-known-good 配置。如果返回空 JSON，其 hash 与 Leader 配置不同，Traefik 会应用空配置并清除所有路由。503 是更安全的选择。

**为什么不使用 Standby 本地缓存**：

Standby 也会收到 MetaStore watch 事件并更新本地 `TraefikRouteCache`，但：
1. Watch 事件有传播延迟，standby 可能滞后
2. Leader 切换期间，旧 standby 的缓存可能已与新状态不一致
3. 转发保证单一事实来源

### 5. 实例生命周期集成

通过 `InstanceManagerActor` 的 MetaStore watch 回调驱动路由表更新：

```
MetaStore Watch Event
    │
    ▼
InstanceManagerActor::OnInstanceWatchEvent()
    │
    ├── EVENT_TYPE_PUT + state=RUNNING
    │       → TraefikRouteCache::OnInstanceRunning(instance)
    │           → 解析 portForward，提取 IP，写入 routeTable_
    │
    ├── EVENT_TYPE_PUT + state=FATAL/EVICTED/EXITED
    │       → TraefikRouteCache::OnInstanceExited(instanceID)
    │           → 从 routeTable_ 移除
    │
    └── EVENT_TYPE_DELETE
            → TraefikRouteCache::OnInstanceExited(instanceID)
```

## 线程安全

| 组件 | 锁类型 | 保护对象 | 说明 |
|------|--------|----------|------|
| `routeTable_` | `shared_mutex` | 路由表读写 | 写：OnInstanceRunning/Exited；读：BuildConfigJSON |
| `dirty_` | `atomic<bool>` | 缓存失效标记 | 无锁快速路径 |
| `cachedJSON_` | `mutex` | 缓存 JSON 字符串 | 重建和读取互斥 |
| `isLeader` | `atomic<bool>` | Leader 状态 | HTTP handler 每次请求检查，需零开销 |
| `leaderHttpAddress` | `shared_mutex` | Leader 地址 | 多读少写（Explorer 回调更新） |

## 配置参数

| Flag | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enable_traefik_provider` | bool | false | 启用 HTTP provider 端点 |
| `traefik_http_entry_point` | string | "websecure" | Traefik entryPoint 名称 |
| `traefik_enable_tls` | bool | true | 生成的 router 是否启用 TLS |
| `traefik_servers_transport` | string | "yr-backend-tls@file" | HTTPS 后端的 serversTransport |
| `traefik_forward_timeout_ms` | uint32 | 3000 | Standby→Leader 转发超时，必须 < Traefik pollTimeout (5s) |

## 部署变更

| 文件 | 变更 |
|------|------|
| `functionsystem/.../flags.h/cpp` | 新增 5 个 flag 定义 |
| `functionsystem/.../install.sh` | 启动命令传入 traefik 相关参数 |
| `deploy/process/config.sh` | getopt 解析、默认值、export |

## 测试覆盖

### traefik_route_cache_test.cpp

覆盖场景：
- 空缓存返回合法空配置（无 routers/services key）
- 新格式/旧格式端口映射解析
- 多端口实例
- 实例移除后路由清理
- 输入校验：无 portForward、无效地址、空数组、畸形 JSON
- ID 清洗：特殊字符替换、超长截断
- 缓存行为：多次调用逐字节相同、变更后失效
- TLS 开关、自定义 entryPoint/serversTransport
- **并发测试**：20 实例、4 线程并发读写删除，无数据竞争
- **Traefik 兼容回归测试**（`TraefikCompat_*`）：空 map 省略、添加后移除回归、HTTP 后端无 TLS 字段

### global_sched_driver_test.cpp

覆盖 GlobalSchedDriver 初始化、健康检查端点、Traefik 端点注册。

## 故障模式

| 场景 | 行为 | Traefik 状态 |
|------|------|-------------|
| Leader 正常，standby 收到 poll | 转发成功，返回 Leader 配置 | 应用最新配置 |
| Leader 宕机，standby 收到 poll | 转发超时 → 503 | 保留上次有效配置 |
| 尚未选出 Leader | leaderHttpAddress 为空 → 503 | 保留上次有效配置 |
| 自环检测（地址 = 自身） | 503 | 保留上次有效配置 |
| 脑裂（两节点都认为自己是 Leader） | 各自提供本地缓存 | 降级但不中断（概率极低） |
