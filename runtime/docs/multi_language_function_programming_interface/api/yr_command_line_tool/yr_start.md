# `start`

在主机上手动部署 openYuanrong。

## 用法

```shell
yr start [flags]
```

## 参数

参数详见 [部署参数表](../../../deploy/deploy_processes/parameters.md)

## 前提条件

* 已安装 openYuanrong。

## Example

部署主节点：

```shell
yr start --master
```

记录打印的 "Cluster master info" 内容，运行以下命令即可将一个从节点加入到 openYuanrong 集群。

```shell
yr start --master_info master_ip:xx.xx.xx.xx,etcd_ip:xx.xx.xx.xx,etcd_port:13639,global_scheduler_port:11180,ds_master_port:14575,etcd_peer_port:17388,bus-proxy:22042,bus:28261,ds-worker:30209,
```
