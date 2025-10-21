# 告警指标

openYuanrong 通过指标提供告警能力，当故障（内外部组件）发生时，openYuanrong 可将故障信息导出至指定平台，便于用户及时运维。

| 指标名称 | 指标含义 | 触发条件 |
| ---------- | -------------------- | -------------------- |
| yr_proxy_alarm                  | 调度器异常。   | 心跳探测（周期为 `systemTimeout/12`，单位为 ms，`systemTimeout` 默认值 `60000`）失败时上报告警，之后不再检测心跳。 |
| yr_etcd_alarm                   | etcd 异常。   | 心跳探测（周期为 `metaStoreCheckHealthIntervalMs`，单位为 ms, 默认值 `5000`）失败时上报告警，之后持续检测心跳。若检测失败持续告警，恢复后消警。 |
| yr_election_alarm               | 选主异常。     | openYuanrong 组件（function-master）开启选主时，选主失败则上报告警（当前仅支持使用 txn 选主模式场景下告警），恢复后消警。 |
