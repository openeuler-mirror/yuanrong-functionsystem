# metrics

- [编译](#编译)
- [使用 SDK](#使用 SDK)
    - [初始化从 so 中加载 exporter](#初始化从 so 中加载 exporter)
    - [初始化后创建 gauge 采集](#初始化后创建 gauge 采集)

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

