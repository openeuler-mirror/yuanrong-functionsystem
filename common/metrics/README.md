# metrics

- [编译](#编译)
- [使用 SDK](#使用 SDK)
    - [初始化从 so 中加载 exporter](#初始化从 so 中加载 exporter)
    - [初始化后创建 gauge 采集](#初始化后创建 gauge 采集)
    - [告警导出到 OpenTelemetry Collector](#告警导出到-opentelemetry-collector)
    - [当前告警类型清单](#当前告警类型清单)
    - [新增告警的方法](#新增告警的方法)

## 编译

### 查看编译帮助

```shell
# bash build.sh --help
```

## 使用 SDK

### 导出

用户通过设置 `ExporterOptions` 参数设置数据导出的模式

#### 单条导出`Simple`

数据采集完成后立即导出

#### 批量导出`Batch`

- 导出条数`batchSize`: metrics 数据缓存到一定数量时全部导出
- 隔导出时间间隔`batchIntervalSec`: 每到 x 秒会导出所有数据，及时没有达到导出条数

### 初始化从 so 中加载 exporter

```c++
#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_library_handle_unix.h"
#include "metrics/plugin/dynamic_load.h"
#include "metrics/sdk/immediately_export_processor.h"
#include "metrics/sdk/meter_provider.h"

namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;
namespace MetricsSDK = observability::sdk::metrics;

// 初始化MeterProvider
auto mp = std::make_shared<MetricsSDK::MeterProvider>();

// 创建ostream-exporter并设为导出器并设置batch导出模式，目前已实现：ostream-exporter/file-exporter/pushgateway-exporter
std::string error;
auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibPath("libobservability-metrics-exporter-ostream.so"), "", error);

MetricsSDK::ExportConfigs exportConfigs;
exportConfigs.exporterName = "batchExporter";
exportConfigs.exporterName = MetricsSDK::ExportMode::BATCH;
auto processor = std::make_unique<MetricsSDK::BatchExportProcessor>(std::move(exporter), exportConfigs);
mp->AddMetricProcessor(std::move(processor));
MetricsApi::Provider::SetMeterProvider(mp);
```

### 初始化后创建 gauge 采集

#### 单次上报数据，创一个名为"cpu_usage" 的 gauge 数据

```text
auto provider = MetricsApi::Provider::GetMeterProvider();
auto meter = provider->GetMeter("cpu_usage");
auto cpuGauge = meter->CreateDoubleGauge("cpu_usage", "CPU Usage", "%");

MetricsSDK::PointLabels labels;
labels.emplace_back(std::make_pair("node_id", "127.0.0.1"));
double val = MockGetCpuUsage;
cpuGauge->Set(val, labels);
```

#### 周期性上报数据，创建一个1s定期上报的名为 "interval_1_disk_usage"的 Counter 采集

```text
// 带回调模式，每次采集器时回调值写入相应的度量中。
auto diskGauge = meter->CreateUint64ObservableCounter("interval_1_disk_usage", "Disk Usage", "MB", 10,
    [](observability::metrics::ObserveResult ob_res) {
        if (std::holds_alternative<std::shared_ptr<observability::metrics::ObserveResultT<uint64_t>>>(ob_res)) {
            uint64_t value = MockGetDiskUsage();
            std::get<std::shared_ptr<observability::metrics::ObserveResultT<Uint64>>>(ob_res)->Observe(value);
        }
    });
```

### 告警导出到 OpenTelemetry Collector

告警沿用现有 `metrics_sdk` 对外 API，不新增新的告警上报接口。当前 OTEL exporter 会把告警继续作为 metrics 上报到 collector，同时额外把存量告警补充为统一的 OTEL attributes，便于 collector / agent 侧直接消费。

- 原始 labels 会原样保留
- 标准告警（`Alarm::Set` 写入 `yrAlarmLabelKey` JSON）会展开为：
  - `yr.event.type=alarm`
  - `yr.alarm.id`
  - `yr.alarm.name`
  - `yr.alarm.severity`
  - `yr.alarm.location_info`
  - `yr.alarm.cause`
  - `yr.alarm.starts_at`
  - `yr.alarm.ends_at`
  - `yr.alarm.timeout`
  - 以及 `customOptions` 对应的 `yr.alarm.<key>`
- 历史兼容告警（`alarm_meter_gauge` 标签告警）会按相同规则归一化，其中：
  - `level -> yr.alarm.severity`
  - `start_timestamp -> yr.alarm.starts_at`
  - `end_timestamp -> yr.alarm.ends_at`
  - 其余标签按 `yr.alarm.<key>` 补充

示例配置：

```json
{
  "enabledMetrics": [
    "yr_k8s_alarm",
    "yr_proxy_alarm",
    "yr_etcd_alarm",
    "yr_metastore_alarm",
    "yr_election_alarm",
    "yr_instance_create_failure_alarm",
    "yr_token_rotation_failure_alarm",
    "yr_obs_alarm",
    "yr_pod_alarm"
  ],
  "backends": [
    {
      "immediatelyExport": {
        "name": "Alarm",
        "enable": true,
        "custom": {
          "labels": {
            "site": "cn-north-7",
            "tenant_id": "tenant-001",
            "application_id": "app-001",
            "service_id": "svc-001"
          }
        },
        "exporters": [
          {
            "opentelemetryExporter": {
              "enable": true,
              "enabledInstruments": [
                "yr_k8s_alarm",
                "yr_proxy_alarm",
                "yr_etcd_alarm",
                "yr_metastore_alarm",
                "yr_election_alarm",
                "yr_instance_create_failure_alarm",
                "yr_token_rotation_failure_alarm",
                "yr_obs_alarm",
                "yr_pod_alarm",
                "alarm_meter_gauge"
              ],
              "initConfig": {
                "endpoint": "http://otel-collector:4318/v1/metrics",
                "protocol": "http",
                "timeout": 10000
              }
            }
          }
        ]
      }
    }
  ]
}
```

说明：

1. `enabledMetrics` 决定 functionsystem 内部哪些具名告警会被启用。
2. `enabledInstruments` 决定 exporter 实际导出哪些 instrument；如果需要兼容 `StsUnhealthyFiring` 这类历史告警，必须额外包含 `alarm_meter_gauge`。
3. OTEL endpoint 建议直接指向 collector 的 metrics OTLP HTTP 地址，例如 `/v1/metrics`。

### 当前告警类型清单

| 告警 instrument | 触发入口 | 说明 |
| --- | --- | --- |
| `yr_k8s_alarm` | `MetricsAdapter::SendK8sAlarm` | K8s 异常告警 |
| `yr_proxy_alarm` | `MetricsAdapter::SendSchedulerAlarm` | 调度/代理异常告警 |
| `yr_etcd_alarm` | `MetricsAdapter::StorageBackendUnhealthyFiring/Resolved(..., "Etcd")` | etcd 连接异常/恢复 |
| `yr_metastore_alarm` | `MetricsAdapter::StorageBackendUnhealthyFiring/Resolved(..., "Metastore")` | metastore 连接异常/恢复 |
| `yr_election_alarm` | `MetricsAdapter::ElectionFiring/Resolved` | 主从选举异常/恢复 |
| `yr_token_rotation_failure_alarm` | `MetricsAdapter::SendTokenRotationFailureAlarm` | STS token 轮换失败 |
| `yr_obs_alarm` | `MetricsAdapter::SendS3Alarm` | OBS / S3 异常告警 |
| `yr_pod_alarm` | `MetricsAdapter::SendPodAlarm` | Pod 异常告警 |
| `yr_instance_create_failure_alarm` | `MetricsAdapter::SendInstanceCreateFailureAlarm` | 实例创建阶段最终失败（当前覆盖 readiness / runtime 初始化失败） |
| `alarm_meter_gauge` | `MetricsAdapter::StsUnhealthyFiring` | 历史兼容标签告警，导出到 OTEL 时会归一化为 `yr.alarm.*` attributes |

### 新增告警的方法

推荐沿用现有“具名 alarm + `AlarmInfo`”模式，不修改外部 API：

1. **定义告警常量**
   - 在 `functionsystem/src/common/metrics/metrics_constants.h` 中新增 instrument 名称。
   - 如果该告警需要受 `enabledMetrics` 控制，同步补充 `YRInstrument`、`INSTRUMENT_DESC_2_ENUM`、`ENUM_2_INSTRUMENT_DESC`。
2. **补充触发逻辑**
   - 简单告警：在 `functionsystem/src/common/metrics/alarm_handler.h/.cpp` 中增加发送函数，构造 `AlarmInfo` 后调用 `meter->CreateAlarm(...)->Set(...)`。
   - 需要 firing / resolved 状态管理的告警：在 `functionsystem/src/common/metrics/metrics_adapter.cpp` 中参考 `HandleBackendStorageAlarm` 或 `HandleElectionAlarm`，维护 `metricsContext_` 中的活动告警。
3. **补充上下文字段**
   - 需要 collector 侧检索的字段尽量放入 `AlarmInfo` 基础字段或 `customOptions`。
   - OTEL exporter 会自动把这些字段展开为 `yr.alarm.*` attributes，无需修改对外告警 API。
4. **更新配置**
   - 在 `enabledMetrics` 中启用新告警。
   - 在对应 exporter 的 `enabledInstruments` 中加入新 instrument。
   - 如果继续复用历史 `alarm_meter_gauge` 模式，也要确保 `enabledInstruments` 包含 `alarm_meter_gauge`。
5. **补测试**
   - `common/metrics/test/unit/metric/alarm_test.cpp`：验证 `AlarmInfo -> yrAlarmLabelKey` 序列化。
   - `common/metrics/test/unit/exporter/opentelemetry_exporter_test.cpp`：验证 OTEL attributes 归一化。
   - `functionsystem/tests/unit/common/metrics/metrics_adapter_test.cpp`：验证具体告警入口、上下文与 firing/resolved 分支。
