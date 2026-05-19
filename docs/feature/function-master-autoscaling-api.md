# Function-master 扩缩容运维接口

本文档说明 function-master 为集群扩缩容提供的调度队列、节点调度开关和实例查询接口。

## 1. 获取调度队列

- **路径**：`GET /global-scheduler/scheduling_queue`
- **说明**：返回当前 root domain 中等待调度的实例请求，包括普通实例队列和 group 队列。

### 返回字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `count` | number | 当前返回的排队实例数量，等于 `instanceInfos` 长度 |
| `instanceInfos` | array | 排队中的实例信息 |
| `instanceInfos[].resources` | object | 该实例等待的资源请求 |
| `instanceInfos[].enqueueTimeMs` | string | 实例进入调度队列的 Unix 毫秒时间戳 |
| `instanceInfos[].waitDurationMs` | string | 从入队到查询时刻的等待时长，单位毫秒 |

### 示例

```bash
curl -s http://127.0.0.1:8080/global-scheduler/scheduling_queue | jq
```

```json
{
  "count": 2,
  "instanceInfos": [
    {
      "instanceID": "req-a-instance",
      "requestID": "req-a",
      "resources": {},
      "enqueueTimeMs": "1715060000000",
      "waitDurationMs": "183"
    }
  ]
}
```

## 2. 控制指定节点的本地调度状态

- **路径**：`/global-scheduler/node/localschedulingstatus`
- **说明**：由 function-master 转发到指定节点的 local-scheduler，切换该节点是否允许继续调度新实例。

### 请求参数

| 参数 | 位置 | 必填 | 说明 |
| --- | --- | --- | --- |
| `node_id` | query | 是 | 目标 local-scheduler 节点 ID |

### 请求方法

| 方法 | 语义 |
| --- | --- |
| `POST` | 将节点切到 `evicting`，停止接收新的本地调度 |
| `DELETE` | 将节点恢复到 `normal`，重新允许本地调度 |

### 返回字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `status` | string | 目标状态，取值为 `evicting` 或 `normal` |
| `message` | string | 执行结果，成功时为 `success` |

### 示例

```bash
curl -X POST "http://127.0.0.1:8080/global-scheduler/node/localschedulingstatus?node_id=node-a"
curl -X DELETE "http://127.0.0.1:8080/global-scheduler/node/localschedulingstatus?node_id=node-a"
```

## 3. 按节点查询实例

- **路径**：`GET /instance-manager/query-tenant-instances`
- **说明**：现已支持按 `node_id` 过滤指定节点上的实例；可与 `tenant_id`、`instance_id`、分页参数组合使用。

### 请求参数

| 参数 | 必填 | 说明 |
| --- | --- | --- |
| `tenant_id` | 是 | 租户 ID；当值为 system tenant 时返回所有租户实例 |
| `instance_id` | 否 | 指定实例 ID |
| `node_id` | 否 | 指定节点 ID，匹配 `instance.functionProxyID` |
| `page` | 否 | 页码，从 1 开始 |
| `page_size` | 否 | 每页数量 |

### 返回字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `count` | number | 满足过滤条件的实例总数 |
| `instances` | array | 当前页实例列表 |
| `tenantID` | string | 请求中的租户 ID |
| `instanceID` | string | 请求中指定的实例 ID（有传参时返回） |
| `nodeID` | string | 请求中指定的节点 ID（有传参时返回） |
| `page` / `pageSize` | number | 分页开启时返回 |
| `isSystemTenant` | boolean | system tenant 查询时返回 |

### 示例

```bash
curl -s "http://127.0.0.1:8080/instance-manager/query-tenant-instances?tenant_id=t1&node_id=node-a&page=1&page_size=20" | jq
```
