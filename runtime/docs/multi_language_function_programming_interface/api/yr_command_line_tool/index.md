# CLI - the `yr` command

`yr` 提供了一系列命令协助您使用 openYuanrong。

## 部署

| 命令 | 说明 |
| ---- | ---- |
| [`start`](./yr_start.md) | 在主机上部署 openYuanrong，拉起 openYuanrong 相关进程。 |
| [`stop`](./yr_stop.md) | 在主机上停止 openYuanrong 相关进程。 |

## 观测

| 命令 | 说明 |
| ---- | ---- |
| [`status`](./yr_status.md) | 查看集群状态（仅支持通过 `yr start` 拉起的 openYuanrong 集群）。|

## 其他

| 命令 | 说明 |
| ---- | ---- |
| [`version`](./yr_version.md) | 输出 CLI 版本相关信息。 |
| [`completion`](./yr_completion.md) | 用于配置 CLI 自动补全。 |

```{eval-rst}
.. toctree::
  :hidden:

  yr_start
  yr_stop
  yr_status
  yr_version
  yr_completion
```
