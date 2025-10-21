# 入门

本节将演示使用默认配置参数在一台或者多台 Linux 主机上部署 openYuanrong ，建议用于学习和开发，生产部署请参考[用户指南](./production/index.md)。

## 部署 openYuanrong

首先参考[安装指南](../installation.md)在所有部署主机上安装 openYuanrong 命令行工具 yr，我们将使用它部署 openYuanrong。

任选一台主机，使用如下命令部署[主节点](glossary-master-node)。

```bash
yr start --master
```

部署成功会打印如下主节点信息。

```bash
Cluster master info:
    local_ip:x.x.x.x,master_ip:x.x.x.x,etcd_ip:x.x.x.x,etcd_port:15903,global_scheduler_port:11188,ds_master_port:12729,cluster_deployer_port:22775,etcd_peer_port:17621,bus-proxy:30140,bus:32480,ds-worker:33210,
```

此时，openYuanrong 服务已经可以使用。需要多节点集群部署时，参考如下命令，在其余主机上使用命令行工具 yr 部署[从节点](glossary-agent-node)。

```bash
# 使用前一步骤打印的主节点信息替换引号中的内容。
$ yr start --master_info "local_ip:x.x.x.x,master_ip:x.x.x.x,etcd_ip:x.x.x.x,etcd_port:15903,global_scheduler_port:11188,ds_master_port:12729,cluster_deployer_port:22775,etcd_peer_port:17621,bus-proxy:30140,bus:32480,ds-worker:33210,"
```

在节点任一主机上执行 `yr status` 命令可查看集群状态。正常情况下，`current running agents` 的数量和实际部署的节点数量一致。

```bash
# x.x.x.x:15903替换为部署步骤打印的etcd_ip和etcd_port
$ yr status --etcd_endpoint x.x.x.x:15903
YuanRong cluster addresses:
                    bus: x.x.x.x:22773
                 worker: x.x.x.x:31501

YuanRong cluster status:
  current running agents: 2
```

可运行[简单示例](../../multi_language_function_programming_interface/examples/simple-function-template.md)进一步验证部署结果。

## 删除 openYuanrong 集群

使用命令行工具 yr 在**所有部署节点**上执行如下命令：

```bash
yr stop
```

:::{note}
`yr stop` 命令会触发 openYuanrong 进程优雅退出，最长等待 5 秒，之后 openYuanrong 进程会被强制清除。
:::
