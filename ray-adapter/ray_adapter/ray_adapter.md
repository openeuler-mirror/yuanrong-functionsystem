# ray_adapter 

兼容开源软件 Ray 的核心接口，可以将运行在 Ray 上的工作负载（如 vllm/verl 等）无缝迁移到 openYuanRong 集群上，同时享受 openYuanRong 在华为鲲鹏和 Ascend 硬件上深度优化带来的性能优势。
openYuanRong 的安装部署请参考文档: https://pages.openeuler.openatom.cn/openyuanrong/zh_cn/latest/index.html。

:::{Note}

使用时将应用代码中的 import ray 替换为 import ray_adapter as ray，并关注接口的差异。

:::

### ray_adapter 接口与 ray 接口的差异说明

| 接口名称                    | 与 Ray接口 的差异                                                                        |
|-----------------------------|------------------------------------------------------------------------------------|
| `remote`                    | 支持参数解析，ray_adapter 目前支持参数有：`num_cpus`, `num_npus`, `resources`, `concurrency_groups`, `max_concurrency` |
| `method`                    | 目前仅支持`num_returns`参数                                                               |
| `nodes`                     | 相同                                                                                 |
| `available_resources`       | 相同                                                                                 |
| `cluster_resources`         | 相同                                                                                 |
| `get`                       | 相同                                                                                 |
| `is_initialized`            | 相同                                                                                 |
| `init`                      | 目前定义参数只有`logging_level`, `num_cpus`, `runtime_env`                                 |
| `kill`                      | 相同                                                                                 |
| `get_actor`                 | 返回的是自定义 ActorHandle 对象，而非 Ray 原生 actor handle                                 |
| `util.get_node_ip_address`  | 相同                                                                                 |
| `util.list_named_actors`    | 相同                                                                                 |
| `runtime_context().get_accelerator_ids` | 记录了 npu 和 device 信息                                                                |                                                                                                             |
| `runtime_context().get_node_id`         | 目前输出效果为id + 进程号                                                                    |
| `runtime_context().namespace`           | 目前无法获取 actor 的命名空间，ray_adapter 目前定义接口返回空                                               |

#### remote 示例

```python
import ray_adapter as ray
ray.init()
@ray.remote(num_cpus=1, max_retries=3)
def test_function():
    return "Hello!"
remote_function = test_function.remote()
result = ray.get(remote_function)
print(result)
ray.shutdown()
```

#### nodes 示例

```python
import ray_adapter as ray
ray.init()
node_info = ray.nodes()
for node in node_info:
    print(node)
ray.shutdown()
```

#### available_resources 示例

```python
import ray_adapter as ray
ray.init()
resources = ray.available_resources()
print(resources)
ray.shutdown()
```

#### cluster_resources 示例

```python
import ray_adapter as ray
ray.init()
cluster = ray.cluster_resources()
print(cluster)
ray.shutdown()
```

#### get 示例

```python
import ray_adapter as ray
ray.init()
@ray.remote()
def add(a, b):
    return a + b
obj_ref_1 = add.remote(1, 2)
obj_ref_2 = add.remote(3, 4)
result = ray.get([obj_ref_1, obj_ref_2], timeout=-1)
print(result)
ray.shutdown()
```

#### is_initialized 示例

```python
ray.init_spark(..., placement_group=pg)
```

#### method 示例

```python
import ray_adapter as ray
ray.init()
@ray.remote()
class f():
    @ray.method(num_returns=1)
    def method0(self):
        return 1


a = f.remote()
id = a.method0.remote()
print(ray.get(id))
ray.shutdown()
```

#### init 示例

```python
import ray_adapter as ray
ray.init()
ray.shutdown()
```

### kill 示例

```python
import ray_adapter as ray
ray.init()
@ray.remote
class Actor:
    def fun(self):
        return 2
a = Actor
ray.kill(a)
```

### shutdown 示例

```python
import ray_adapter as ray
ray.init()
ray.shutdown()
```

### available_resources_per_node 示例

```python
import ray_adapter as ray
ray.init()
res = ray.available_resources_per_node()
print(res)
```

### get_actor 示例

```python
import ray_adapter as ray
ray.init()
actor = ray.get_actor("test_actor")
print(actor)
ray.shutdown()
```

### util.get_node_ip_address 示例

```python
import ray_adapter as ray
ray.init()
node_ip = ray.util.get_node_ip_address()
print(node_ip)
```

### util.list_named_actors 示例

```python
import ray_adapter as ray
ray.init()
named_actors = ray.list_named_actors()
print(named_actors)
```

### runtime_context().get_accelerator_ids 示例

```python
import ray_adapter as ray
ray.init()
result = ray.runtime_context().get_accelerator_ids()
print(result)
```

### runtime_context().get_node_id 示例

```python
import ray_adapter as ray
ray.init()
result = ray.runtime_context().get_node_id()
print(result)
```

### runtime_context().namespace 示例

```python
import ray_adapter as ray
ray.init()
@ray.remote()
class A:
    def hello(self):
        return "hi"
a = A.options(name="aaaabbb").remote()
ray.get(a.hello.remote())
res = ray.util.list_named_actors()
print(res)
```






















