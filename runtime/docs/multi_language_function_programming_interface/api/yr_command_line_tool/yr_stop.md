# `stop`

停止主机上的 openYuanrong 进程。

## 用法

```shell
yr stop [flags]
```

## 参数

* `--grace_exit_timeout`：优雅退出时间（单位：秒，默认 5 秒）。超过这个时间后，会强制结束 openYuanrong 进程。

## 前提条件

* 已安装 openYuanrong。

## Example

```shell
yr stop
# find 9 Yuanrong processes
# process etcd(252301) exited .
# process function_proxy(253348) is force killed after 5s
# process deploy.sh(252229) is force killed after 5s
# process function_agent(253502) is force killed after 5s
# process worker(253022) is force killed after 5s
# process dashboard(253517) is force killed after 5s
# process collector(253522) is force killed after 5s
# process function_master(252780) is force killed after 5s
# process worker(252345) is force killed after 5s
# Stop Yuanrong processes costs time 5003ms
# Yuanrong stop succeed.
```

```shell
yr stop
# Did not find any active Yuanrong processes.
```

:::{important} 连续重新启停节点
数据系统组件会将自己的信息注册到 openYuanrong 的 etcd 中，如果发生了数据面的重启，数据系统需要等待 `--ds_node_dead_timeout_s` 以确认节点退出（默认为 30s）。因此，如果在同一个节点反复 `stop` 后，在 `--ds_node_dead_timeout_s` 时间内再通过 `yr start` 拉起新的 openYuanrong 数据面，可能会出现注册错误的情况。建议等待 `--ds_node_dead_timeout_s` 后继续拉起，或将该参数调整为较小的值，但可能会导致网络波动时数据系统组件错误的发生组件消亡的判定。
:::
