# 安装部署

```{eval-rst}
.. toctree::
   :glob:
   :maxdepth: 1
   :hidden:

   installation
   deploy_processes/index
   build_and_deploy
```

## 安装 openYuanrong

开发应用前，需先[安装 openYuanrong](./installation.md)，包括 openYuanrong SDK 和命令行工具 yr 的安装。

## 部署 openYuanrong 集群

openYuanrong 支持在 Linux 主机上以进程方式部署，也支持在 Kubernetes 上以容器方式部署（即将开源）。您可以在单台 Linux 主机或者单节点 Kubernetes 集群上部署 openYuanrong ，用于学习和开发。生产环境中，推荐通过集群方式部署多个节点，以带来更好的性能和可靠性保障，详细介绍参考：

* [在主机上部署](./deploy_processes/index.md)

## 部署 openYuanrong 应用

openYuanrong 支持使用 Python、C++ 及 Java 多语言开发应用，应用通过客户端连接到 openYuanrong 集群运行。

了解如何运行 openYuanrong 应用请参考：[部署 openYuanrong 应用](./build_and_deploy.md)。
