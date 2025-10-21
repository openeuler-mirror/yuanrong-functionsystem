# 指标

```{eval-rst}
.. toctree::
   :glob:
   :maxdepth: 1
   :hidden:

   system-metrics
   alarm-metrics
   export-metrics
```

指标是一组时间序列数据，用于监控 openYuanrong 系统及应用的健康状况和性能，以便及时发现和解决问题。当前 openYuanrong 支持的指标类型如下：

- [系统指标](./system-metrics.md)：包括集群、节点及 openYuanrong 组件的状态信息。
- [告警指标](./alarm-metrics.md)：包括系统及依赖服务的异常信息。

openYuanrong 支持多种指标导出方式，对接您当前的运维平台，详见[配置并获取指标](./export-metrics.md)。
