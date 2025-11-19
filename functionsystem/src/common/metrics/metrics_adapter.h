/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FUNCTIONSYSTEM_METRICSADAPTER_H
#define FUNCTIONSYSTEM_METRICSADAPTER_H

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

#include "common/metrics/alarm_handler.h"
#include "common/metrics/metrics_context.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/singleton.h"
#include "common/utils/ssl_config.h"
#include "metrics/api/gauge.h"
#include "metrics/exporters/exporter.h"
#include "metrics/sdk/meter_provider.h"
#include "metrics/sdk/metric_processor.h"

namespace functionsystem {
namespace metrics {

using LabelType = std::map<std::string, std::string>;
namespace MetricsApi = observability::api::metrics;
namespace MetricsSdk = observability::sdk::metrics;

struct MeterTitle {
    std::string name;
    std::string description;
    std::string unit;
};

struct MeterData {
    double value;
    LabelType labels;
};

class MetricsAdapter : public Singleton<MetricsAdapter> {
public:
    MetricsAdapter() = default;
    ~MetricsAdapter() noexcept override;

    void InitMetricsFromJson(const nlohmann::json &json, const std::function<std::string(std::string)> &getFileName,
                             const SSLCertConfig &sslCertConfig);
    void CleanMetrics() noexcept;
    void SetContextAttr(const std::string &attr, const std::string &value);
    std::string GetContextValue(const std::string &attr) const;

    void ElectionFiring(const std::string &msg);
    void ElectionFiringResolved(const std::string &msg);

    bool HasMetastoreAlarmError();

    bool HasEtcdAlarmError();

    void StorageBackendUnhealthyFiring(const AlarmLevel level, const std::string &errMsg = "",
                                const std::string &backend = "");
    void StorageBackendUnhealthyResolved(const AlarmLevel level, const std::string &backend);
    void StsUnhealthyFiring(const std::string &errMsg);

    void ReportClusterSourceState(const std::shared_ptr<resource_view::ResourceUnit> &unit);

    void ReportGauge(const struct MeterTitle &title, struct MeterData &data);

    void ReportDoubleGauge(const struct MeterTitle &title, struct MeterData &data,
                           const std::list<std::string> &contextAttrs);

    void ReportInstanceExitLatency(const Status &status, long long startTimeMillis,
                                   const resource_view::InstanceInfo &instanceInfo);

    MetricsContext &GetMetricsContext();
    AlarmHandler &GetAlarmHandler()
    {
        return alarmHandler_;
    }

    void ReportInstanceStatus(const std::string &instanceID, const resource_view::InstanceInfo &instanceInfo);

    void ReportBillingInvokeLatency(const std::string &requestID, uint32_t errCode, long long startTimeMillis,
                                     long long endTimeMillis);

    static std::vector<std::string> ConvertNodeLabels(const NodeLabelsType &nodeLabels);

    void RegisterInstanceMetrics();

    void RegisterInstCPUUsage();

    void RegisterInstMemoryUsage();

    void RegisterInstNPUUsage();

    void CollectInstanceMetrics(MetricsApi::ObserveResult obRes, const std::string &reourceName);

    std::string GenerateInstCardDetails(const resources::Resource &resource, std::string &nodeID);

    void RegisterPhysicalMetricsCounter();

    void RegisterPhysicalNodeCPUUtilization();

    void RegisterPhysicalNodeMemoryUsage();

    void RegisterPhysicalNodeMemoryCapacity();

    void RegisterPhysicalNodeDiskUsage();

    void RegisterPhysicalNodeDiskCapacity();

    void RegisterPhysicalNodeNpu();

    void CollectPhysicalNodeNPUData(MetricsApi::ObserveResult obRes);

    void CollectGeneralMetricsNodeData(MetricsApi::ObserveResult obRes, const std::string &reourceName,
                                        bool isUageMetrics, LabelType baseLabelMap = {});

    void RegisterBillingInstanceRunningDuration();
    void CollectBillingInstanceRunningDuration(MetricsApi::ObserveResult obRes);

    void RegisterPodResource();
    void CollectPodResource(MetricsApi::ObserveResult obRes);

    void SendK8sAlarm(const std::string &locationInfo);
    void SendSchedulerAlarm(const std::string &locationInfo);
    void SendTokenRotationFailureAlarm();
    void SendS3Alarm();
    void SendPodAlarm(const std::string &podName, const std::string &cause);

    // for test
    [[maybe_unused]] std::map<std::string, std::shared_ptr<observability::api::metrics::ObservableInstrument>>
    GetObservableInstrumentMap()
    {
        return observableInstrumentMap_;
    }

    // for test
    [[maybe_unused]] void SetEnabledBackends(const std::string &backend)
    {
        enabledBackends_.insert(backend);
    }

    [[maybe_unused]] void ClearEnabledInstruments()
    {
        enabledInstruments_.clear();
    }

private:
    void SetEnabledInstruments();
    const MetricsSdk::ExportConfigs BuildExportConfigs(const nlohmann::json &exporterValue);
    void InitExport(const MetricsSdk::ExportMode &exportMode,
                    const std::shared_ptr<observability::sdk::metrics::MeterProvider> &mp,
                    const nlohmann::json &backendValue, const std::function<std::string(std::string)> &getFileName,
                    const SSLCertConfig &sslCertConfig);
    void SetImmediatelyExporters(const std::shared_ptr<observability::sdk::metrics::MeterProvider> &mp,
                                 const std::string &backendName, const nlohmann::json &exporters,
                                 const std::function<std::string(std::string)> &getFileName,
                                 const SSLCertConfig &sslCertConfig);
    void SetBatchExporters(const std::shared_ptr<observability::sdk::metrics::MeterProvider> &mp,
                           const std::string &backendName, const nlohmann::json &exporters,
                           const std::function<std::string(std::string)> &getFileName,
                           const SSLCertConfig &sslCertConfig);
    std::shared_ptr<observability::exporters::metrics::Exporter> InitFileExporter(
        const std::string &backendKey, const std::string &backendName, const nlohmann::json &exporterValue,
        const std::function<std::string(std::string)> &getFileName);

    std::shared_ptr<observability::exporters::metrics::Exporter> InitHttpExporter(const std::string &httpExporterType,
        const std::string &backendName, const nlohmann::json &exporterValue, const SSLCertConfig &sslCertConfig);
    void InitAlarmGauge();
    void AlarmGaugeLabelsAddContextAttr(MetricsApi::MetricLabels &labels);
    void AlarmGaugeLabelsAddBaseAttr(const std::string &id, const std::string &name, const std::string &level,
                                     MetricsApi::MetricLabels &labels);
    void AlarmGaugeLabelsAddTimeStamp(const std::string &start, const std::string &end,
                                      MetricsApi::MetricLabels &labels);

    void InitDoubleGauge(const struct MeterTitle &title);
    void InitObservableCounter(const struct MeterTitle &title, int interval, MetricsApi::CallbackPtr &cb,
                               observability::sdk::metrics::InstrumentValueType observableType);
    void InitObservableGauge(const struct MeterTitle &title, int interval, MetricsApi::CallbackPtr &cb,
                             observability::sdk::metrics::InstrumentValueType observableType);
    std::string GetSourceTag() const;
    void AddAlarmCommonAttrs(MetricsApi::AlarmInfo &etcdAlarmInfo);
    void HandleBackendStorageAlarm(bool isFiring, const AlarmLevel level, const std::string &msg = "",
        const std::string &backend = "");
    void HandleElectionAlarm(bool isFiring, const std::string &msg);

    void TransformGaugeParam(const std::string &name, const std::string &description, const std::string &unit,
                             const double value);

    void ExtractValueFromVec(
        google::protobuf::Map<std::__cxx11::basic_string<char>, resources::Value_Vectors_Category> &vec,
        const std::string &key, std::string &nodeID, google::protobuf::RepeatedField<double> &value);

    static std::string GetResourceScalar(const resources::Resources &resource, const std::string &resType);

    static std::string GetResourceUsedScalar(const resources::Resources &capacity,
                                             const resources::Resources &allocatable, const std::string &resType);

    std::pair<MetricsApi::MetricLabels, uint64_t> BuildBillingInstanceRunningDurationData(
        const std::string &instanceID, const BillingInstanceInfo &billingInstanceInfo, bool extraBilling);

    static std::pair<MetricsApi::MetricLabels, double> BuildPodResourceData(const std::string &agentID,
                                                                            const PodResource &podResource);

    MetricsContext metricsContext_;
    std::unique_ptr<observability::api::metrics::Gauge<uint64_t>> alarmGauge_{ nullptr };
    bool enableMetrics_{ false };
    std::unordered_set<std::string> enabledBackends_;
    std::unordered_set<YRInstrument> enabledInstruments_;

    std::mutex mutex_{};
    std::map<std::string, std::unique_ptr<observability::api::metrics::Gauge<double>>> doubleGaugeMap_{};
    std::map<std::string, std::shared_ptr<observability::api::metrics::ObservableInstrument>>
        observableInstrumentMap_{};

    AlarmHandler alarmHandler_;

    std::string GetExportModeDesc(const MetricsSdk::ExportMode &mode);

    const std::string HBM_KEY_ = "HBM";
    const std::string USED_HBM_KEY_ = "usedHBM";
    const std::string Memory_KEY_ = "memory";
    const std::string USED_MEMORY_KEY_ = "usedMemory";
    const std::string PRODUCT_MODEL_KEY_ = "product_model";
    const std::string IDS_KEY_ = "ids";
    const std::string USED_IDS_KEY_ = "used_ids";
    const std::string NPU_REOURCE_NAME_ = "NPU";
};

}  // namespace metrics
}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_METRICSADAPTER_H
