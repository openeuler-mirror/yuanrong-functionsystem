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

#include "metrics_adapter.h"

#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logs/logging.h"
#include "resource_type.h"
#include "status/status.h"
#include "metrics/api/metric_data.h"
#include "metrics/api/null.h"
#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_library_handle_unix.h"
#include "metrics/sdk/batch_export_processor.h"
#include "metrics/sdk/immediately_export_processor.h"
#include "metrics/sdk/meter_provider.h"
#include "metrics_constants.h"
#include "metrics_context.h"
#include "metrics_utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem {
namespace metrics {
namespace MetricsExporters = observability::exporters::metrics;
namespace MetricsPlugin = observability::plugin::metrics;

const std::unordered_set<std::string> SYSTEM_FUNCTION_NAME = { "0-system-faasscheduler", "0-system-faasfrontend",
                                                               "0-system-faascontroller", "0-system-faasmanager" };
const std::string IMMEDIATELY_EXPORT = "immediatelyExport";
const std::string BATCH_EXPORT = "batchExport";

static std::string GetLibraryPath(const std::string &exporterType)
{
    char path[1024];
    std::string filePath;
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        auto directoryPath = path;
        auto fileName = strrchr(path, '/');
        if (fileName) {
            *fileName = '\0';
        }
        std::string libPath = std::string(directoryPath) + "/../lib/";
        char realLibPath[PATH_MAX] = { 0 };
        if (realpath(libPath.c_str(), realLibPath) == nullptr) {
            YRLOG_WARN("failed to get real path of library {}, errno: {}, {}", libPath, errno,
                       litebus::os::Strerror(errno));
            return "";
        }
        if (exporterType == FILE_EXPORTER) {
            filePath = std::string(realLibPath) + "/libobservability-metrics-file-exporter.so";
        }
        YRLOG_INFO("exporter {} get library path: {}", exporterType, filePath);
    }
    return filePath;
}

const MetricsSdk::ExportConfigs MetricsAdapter::BuildExportConfigs(const nlohmann::json &exporterValue)
{
    try {
        YRLOG_DEBUG("Start to build export config {}", exporterValue.dump());
    } catch (std::exception &e) {
        YRLOG_ERROR("dump exporterValue failed, error: {}", e.what());
    }
    MetricsSdk::ExportConfigs exportConfigs;
    if (exporterValue.contains("batchSize")) {
        exportConfigs.batchSize = exporterValue.at("batchSize");
    }
    if (exporterValue.contains("batchIntervalSec")) {
        exportConfigs.batchIntervalSec = exporterValue.at("batchIntervalSec");
    }
    if (exporterValue.contains("failureQueueMaxSize")) {
        exportConfigs.failureQueueMaxSize = exporterValue.at("failureQueueMaxSize");
    }
    if (exporterValue.contains("failureDataDir")) {
        exportConfigs.failureDataDir = exporterValue.at("failureDataDir");
    }
    if (exporterValue.contains("failureDataFileMaxCapacity")) {
        exportConfigs.failureDataFileMaxCapacity = exporterValue.at("failureDataFileMaxCapacity");
    }
    if (exporterValue.contains("enabledInstruments")) {
        for (auto &it : exporterValue.at("enabledInstruments").items()) {
            YRLOG_INFO("Enabled instrument: {}", it.value());
            exportConfigs.enabledInstruments.insert(it.value());
            enabledInstruments_.insert(GetInstrumentEnum(it.value()));
        }
    }
    return exportConfigs;
}

std::shared_ptr<MetricsExporters::Exporter> MetricsAdapter::InitFileExporter(
    const std::string &backendKey, const std::string &backendName, const nlohmann::json &exporterValue,
    const std::function<std::string(std::string)> &getFileName)
{
    YRLOG_DEBUG("add exporter {} for backend {} of {}", FILE_EXPORTER, backendKey, backendName);
    if (exporterValue.find("enable") == exporterValue.end() || !exporterValue.at("enable").get<bool>()) {
        YRLOG_DEBUG("metrics exporter {} for backend {} of {} is not enabled", FILE_EXPORTER, backendKey, backendName);
        return nullptr;
    }
    std::string initConfig;
    if (exporterValue.find("initConfig") != exporterValue.end()) {
        auto initConfigJson = exporterValue.at("initConfig");
        if (initConfigJson.find("fileDir") == initConfigJson.end() ||
            initConfigJson.at("fileDir").get<std::string>().empty()) {
            YRLOG_DEBUG("not find the metrics exporter file path, use the log path: {}", GetContextValue("log_dir"));
            initConfigJson["fileDir"] = GetContextValue("log_dir");
        }
        if (!litebus::os::ExistPath(initConfigJson.at("fileDir")) &&
            litebus::os::Mkdir(initConfigJson.at("fileDir")).IsSome()) {
            YRLOG_ERROR("failed to mkdir({}) for exporter {} for backend {} of {}, msg: {}",
                        initConfigJson.at("fileDir"), FILE_EXPORTER, backendKey, backendName,
                        litebus::os::Strerror(errno));
            return nullptr;
        }
        if (initConfigJson.find("fileName") == initConfigJson.end() ||
            initConfigJson.at("fileName").get<std::string>().empty()) {
            initConfigJson["fileName"] = getFileName(backendName);
        }
        try {
            initConfig = initConfigJson.dump();
        } catch (std::exception &e) {
            YRLOG_ERROR("dump initConfigJson failed, error: {}", e.what());
        }
    }
    YRLOG_INFO("metrics exporter {} for backend {} of {}, init config: {}", FILE_EXPORTER, backendKey, backendName,
               initConfig);
    std::string error;
    return MetricsPlugin::LoadExporterFromLibrary(GetLibraryPath(FILE_EXPORTER), initConfig, error);
}

std::shared_ptr<MetricsExporters::Exporter> MetricsAdapter::InitHttpExporter(const std::string &httpExporterType,
    const std::string &backendName, const nlohmann::json &exporterValue, const SSLCertConfig &sslCertConfig)
{
    YRLOG_DEBUG("add exporter {} for backend {}", httpExporterType, backendName);
    if (exporterValue.find("enable") == exporterValue.end() || !exporterValue.at("enable").get<bool>()) {
        YRLOG_DEBUG("metrics exporter {} for backend {} is not enabled", httpExporterType, backendName);
        return nullptr;
    }
    std::string initConfig;
    if (exporterValue.find("initConfig") != exporterValue.end()) {
        auto initConfigJson = exporterValue.at("initConfig");
        initConfigJson["jobName"] = metricsContext_.GetAttr("component_name");
        if (initConfigJson.find("ip") != initConfigJson.end() && initConfigJson.find("port") != initConfigJson.end()) {
            initConfigJson["endpoint"] =
                initConfigJson.at("ip").get<std::string>() + ":" + std::to_string(initConfigJson.at("port").get<int>());
        }

        try {
            // print before set ssl config, which can't be printed
            YRLOG_INFO("metrics http exporter for backend {}, initConfig: {}", backendName, initConfigJson.dump());
        } catch (std::exception &e) {
            YRLOG_ERROR("dump initConfigJson failed, error: {}", e.what());
        }

        if (sslCertConfig.isMetricsSSLEnable) {
            initConfigJson["isSSLEnable"] = true;
            initConfigJson["rootCertFile"] = sslCertConfig.rootCertFile;
            initConfigJson["certFile"] = sslCertConfig.certFile;
            initConfigJson["keyFile"] = sslCertConfig.keyFile;
        }
        try {
            initConfig = initConfigJson.dump();
        } catch (std::exception &e) {
            YRLOG_ERROR("dump initConfigJson failed, error: {}", e.what());
        }
    }
    std::string error;
    return MetricsPlugin::LoadExporterFromLibrary(GetLibraryPath(httpExporterType), initConfig, error);
}

void MetricsAdapter::SetImmediatelyExporters(const std::shared_ptr<observability::sdk::metrics::MeterProvider> &mp,
                                             const std::string &backendName, const nlohmann::json &exporters,
                                             const std::function<std::string(std::string)> &getFileName,
                                             const SSLCertConfig &sslCertConfig)
{
    RETURN_IF_NULL(mp);
    std::string exportModeDesc = IMMEDIATELY_EXPORT;
    for (auto &[key, value] : exporters.items()) {
        if (key == FILE_EXPORTER) {
            auto &&exporter = InitFileExporter(exportModeDesc, backendName, value, getFileName);
            if (exporter == nullptr) {
                continue;
            }
            auto exportConfigs = BuildExportConfigs(value);
            exportConfigs.exporterName = metricsContext_.GetAttr("component_name") + "_" + key;
            exportConfigs.exportMode = MetricsSdk::ExportMode::IMMEDIATELY;
            auto processor =
                    std::make_shared<MetricsSdk::ImmediatelyExportProcessor>(std::move(exporter), exportConfigs);
            mp->AddMetricProcessor(std::move(processor));
        } else {
            YRLOG_WARN("unknown exporter name: {}", key);
        }
    }
}

void MetricsAdapter::SetBatchExporters(const std::shared_ptr<observability::sdk::metrics::MeterProvider> &mp,
                                       const std::string &backendName, const nlohmann::json &exporters,
                                       const std::function<std::string(std::string)> &getFileName,
                                       const SSLCertConfig &sslCertConfig)
{
    RETURN_IF_NULL(mp);
    std::string exportModeDesc = BATCH_EXPORT;
    for (auto &[key, value] : exporters.items()) {
        if (key == FILE_EXPORTER) {
            auto &&exporter = InitFileExporter(exportModeDesc, backendName, value, getFileName);
            if (exporter == nullptr) {
                YRLOG_ERROR("Failed to init exporter {}", key);
                continue;
            }
            auto exportConfigs = BuildExportConfigs(value);
            exportConfigs.exporterName = metricsContext_.GetAttr("component_name") + "_" + key;
            exportConfigs.exportMode = MetricsSdk::ExportMode::BATCH;
            auto processor = std::make_shared<MetricsSdk::BatchExportProcessor>(std::move(exporter), exportConfigs);
            mp->AddMetricProcessor(std::move(processor));
        } else {
            YRLOG_WARN("unknown exporter name: {}", key);
        }
    }
}

void MetricsAdapter::InitExport(const MetricsSdk::ExportMode &exportMode,
                                const std::shared_ptr<MetricsSdk::MeterProvider> &mp,
                                const nlohmann::json &backendValue,
                                const std::function<std::string(std::string)> &getFileName,
                                const SSLCertConfig &sslCertConfig)
{
    std::string exportModeDesc = GetExportModeDesc(exportMode);
    YRLOG_DEBUG("metrics add backend {}", exportModeDesc);
    if (backendValue.find("enable") == backendValue.end() || !backendValue.at("enable").get<bool>()) {
        YRLOG_DEBUG("metrics backend {} is not enabled", exportModeDesc);
        return;
    }
    std::string backendName;
    if (backendValue.find("name") != backendValue.end()) {
        backendName = backendValue.at("name");
        YRLOG_DEBUG("metrics add backend {} of {}", exportModeDesc, backendName);
        enabledBackends_.insert(backendName);
    }
    if (backendValue.find("custom") != backendValue.end()) {
        auto custom = backendValue.at("custom");
        if (custom.find("labels") != custom.end()) {
            auto labels = custom.at("labels");
            for (auto &label : labels.items()) {
                YRLOG_DEBUG("metrics backend {} of {} add custom label, key: {}, value: {}", exportModeDesc,
                            backendName, label.key(), label.value());
                metricsContext_.SetAttr(label.key(), label.value());
            }
        }
    }
    if (backendValue.find("exporters") != backendValue.end()) {
        for (auto &[index, exporters] : backendValue.at("exporters").items()) {
            YRLOG_DEBUG("metrics add exporter index({}) for backend {}", index, backendName);
            if (exportMode == MetricsSdk::ExportMode::IMMEDIATELY) {
                SetImmediatelyExporters(mp, backendName, exporters, getFileName, sslCertConfig);
            } else if (exportMode == MetricsSdk::ExportMode::BATCH) {
                SetBatchExporters(mp, backendName, exporters, getFileName, sslCertConfig);
            } else {
                YRLOG_ERROR("Unknown exporter index({}) for backend {}", index, backendName);
            }
        }
    }
}

MetricsAdapter::~MetricsAdapter() noexcept
{
}

void MetricsAdapter::CleanMetrics() noexcept
{
    std::shared_ptr<MetricsApi::NullMeterProvider> null = nullptr;
    MetricsApi::Provider::SetMeterProvider(null);
}

void MetricsAdapter::InitMetricsFromJson(const nlohmann::json &json,
                                         const std::function<std::string(std::string)> &getFileName,
                                         const SSLCertConfig &sslCertConfig)
{
    if (json.find("backends") == json.end()) {
        YRLOG_DEBUG("metrics backends is none");
        return;
    }
    if (json.find("enabledMetrics") != json.end()) {
        for (auto &it : json.at("enabledMetrics").items()) {
            YRLOG_INFO("Enabled instrument: {}", it.value());
            enabledInstruments_.insert(GetInstrumentEnum(it.value()));
        }
    }
    auto mp = std::make_shared<MetricsSdk::MeterProvider>();

    auto backends = json.at("backends");
    for (auto &[index, backend] : backends.items()) {
        YRLOG_DEBUG("metrics add backend index({})", index);
        for (auto &[key, value] : backend.items()) {
            if (key == IMMEDIATELY_EXPORT) {
                InitExport(MetricsSdk::ExportMode::IMMEDIATELY, mp, value, getFileName, sslCertConfig);
            } else if (key == BATCH_EXPORT) {
                InitExport(MetricsSdk::ExportMode::BATCH, mp, value, getFileName, sslCertConfig);
            } else {
                YRLOG_WARN("unknown backend key: {}", key);
            }
        }
    }
    metricsContext_.SetEnabledInstruments(enabledInstruments_);

    MetricsApi::Provider::SetMeterProvider(mp);
}

void MetricsAdapter::InitAlarmGauge()
{
    if (alarmGauge_ != nullptr) {
        return;
    }
    auto provider = MetricsApi::Provider::GetMeterProvider();
    if (provider == nullptr) {
        return;
    }
    std::shared_ptr<MetricsApi::Meter> meter = provider->GetMeter("alarm_meter");
    if (meter == nullptr) {
        return;
    }
    alarmGauge_ = meter->CreateUInt64Gauge("alarm_meter_gauge");
}

void MetricsAdapter::AlarmGaugeLabelsAddContextAttr(MetricsApi::MetricLabels &labels)
{
    labels.emplace_back(std::make_pair<std::string, std::string>("site", metricsContext_.GetAttr("site")));
    labels.emplace_back(std::make_pair<std::string, std::string>("tenant_id", metricsContext_.GetAttr("tenant_id")));
    labels.emplace_back(
        std::make_pair<std::string, std::string>("application_id", metricsContext_.GetAttr("application_id")));
    labels.emplace_back(std::make_pair<std::string, std::string>("service_id", metricsContext_.GetAttr("service_id")));
}

void MetricsAdapter::AlarmGaugeLabelsAddBaseAttr(const std::string &id, const std::string &name,
                                                 const std::string &level, MetricsApi::MetricLabels &labels)
{
    labels.emplace_back(std::pair{ "id", id });
    labels.emplace_back(std::pair{ "name", name });
    labels.emplace_back(std::pair{ "level", level });
}

void MetricsAdapter::AlarmGaugeLabelsAddTimeStamp(const std::string &start, const std::string &end,
                                                  MetricsApi::MetricLabels &labels)
{
    labels.emplace_back(std::pair{ "start_timestamp", start });
    labels.emplace_back(std::pair{ "end_timestamp", end });
}

void MetricsAdapter::ElectionFiring(const std::string &msg)
{
    if (enabledInstruments_.find(YRInstrument::YR_ELECTION_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("election alarm is not enabled");
        return;
    }
    MetricsApi::AlarmInfo electionAlarmInfo;
    electionAlarmInfo.id = "YuanrongElection00001";
    electionAlarmInfo.alarmName = ELECTION_ALARM;
    electionAlarmInfo.alarmSeverity = MetricsApi::AlarmSeverity::MAJOR;

    electionAlarmInfo.cause = msg;
    electionAlarmInfo.startsAt = GetCurrentTimeInMilliSec();
    electionAlarmInfo.endsAt = 0;
    nlohmann::json annotations;
    annotations["err_msg"] = msg;
    try {
        electionAlarmInfo.customOptions["annotations"] = annotations.dump();
    } catch (std::exception &e) {
        YRLOG_ERROR("dump annotations failed, error: {}", e.what());
    }
    electionAlarmInfo.customOptions["op_type"] = "firing";
    electionAlarmInfo.customOptions["source_tag"] = GetSourceTag() + "YuanrongElectionAlarm";

    alarmHandler_.SendElectionAlarm(electionAlarmInfo);
}

void MetricsAdapter::HandleEtcdAlarm(bool isFiring, const AlarmLevel level, const std::string &msg)
{
    if (enabledInstruments_.find(YRInstrument::YR_ETCD_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("etcd alarm is not enabled");
        return;
    }
    if (metricsContext_.GetAttr("component_name") != "function_master") {
        YRLOG_DEBUG("current component is not function master, do not send etcd alarm",
                    metricsContext_.GetAttr("component_name"));
        return;
    }
    MetricsApi::AlarmInfo etcdAlarmInfo;
    etcdAlarmInfo.id = "YuanrongEtcdConnection00001";
    etcdAlarmInfo.alarmName = ETCD_ALARM;
    etcdAlarmInfo.alarmSeverity = alarmHandler_.GetAlarmLevel(level);
    etcdAlarmInfo.customOptions["source_tag"] = GetSourceTag() + "YuanrongEtcdConnection";
    etcdAlarmInfo.cause = msg;
    if (!isFiring) {
        etcdAlarmInfo.startsAt = 0;
        etcdAlarmInfo.endsAt = GetCurrentTimeInMilliSec();
        etcdAlarmInfo.customOptions["op_type"] = "resolved";
    } else {
        etcdAlarmInfo.startsAt = GetCurrentTimeInMilliSec();
        etcdAlarmInfo.endsAt = 0;
        nlohmann::json annotations;
        annotations["err_msg"] = msg;
        try {
            etcdAlarmInfo.customOptions["annotations"] = annotations.dump();
        } catch (std::exception &e) {
            YRLOG_ERROR("dump annotations failed, error: {}", e.what());
        }
        etcdAlarmInfo.customOptions["op_type"] = "firing";
    }
    alarmHandler_.SendEtcdAlarm(etcdAlarmInfo);
}

void MetricsAdapter::EtcdUnhealthyFiring(const AlarmLevel level, const std::string &errMsg)
{
    HandleEtcdAlarm(true, level, "failed to connect to etcd, " + errMsg);
}

void MetricsAdapter::EtcdUnhealthyResolved(const AlarmLevel level)
{
    HandleEtcdAlarm(false, level, "connect to etcd successfully; resolve alarm.");
}

void MetricsAdapter::StsUnhealthyFiring(const std::string &errMsg)
{
    InitAlarmGauge();
    if (alarmGauge_ == nullptr) {
        return;
    }
    MetricsApi::MetricLabels labels;
    AlarmGaugeLabelsAddContextAttr(labels);
    AlarmGaugeLabelsAddBaseAttr("InitStsSdkErr00001", "InitStsSdkErr", "major", labels);

    labels.emplace_back(std::make_pair<std::string, std::string>("source_tag", GetSourceTag() + "|InitStsSdkErr"));
    labels.emplace_back(std::make_pair<std::string, std::string>("op_type", "firing"));
    labels.emplace_back(std::make_pair<std::string, std::string>("details", "Init sts err: " + errMsg));
    labels.emplace_back(std::make_pair<std::string, std::string>("clear_type", "ADAC"));
    AlarmGaugeLabelsAddTimeStamp(GetSystemTimeStampNowStr(), "0", labels);

    alarmGauge_->Set(1, labels);
}

std::string MetricsAdapter::GetSourceTag() const
{
    std::string sourceTag;
    auto podName = litebus::os::GetEnv("POD_NAME");
    if (podName.IsSome()) {
        sourceTag = sourceTag + podName.Get() + "|";
    } else {
        YRLOG_WARN("env POD_NAME is empty, source tag may be incomplete");
    }
    auto podIP = litebus::os::GetEnv("POD_IP");
    if (podIP.IsSome()) {
        sourceTag = sourceTag + podIP.Get() + "|";
    } else {
        YRLOG_WARN("env POD_IP is empty, source tag may be incomplete");
    }
    auto clusterName = litebus::os::GetEnv("CLUSTER_NAME");
    if (clusterName.IsSome()) {
        sourceTag = sourceTag + clusterName.Get();
    } else {
        YRLOG_WARN("env CLUSTER_NAME is empty, source tag may be incomplete");
    }
    return sourceTag;
}

void MetricsAdapter::SendK8sAlarm(const std::string &locationInfo)
{
    if (enabledInstruments_.find(YRInstrument::YR_K8S_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("k8s alarm is not enabled");
        return;
    }
    if (metricsContext_.GetAttr("component_name") == "function_master") {
        YRLOG_DEBUG("{} send k8s alarm", metricsContext_.GetAttr("component_name"));
        alarmHandler_.SendK8sAlarm(locationInfo);
    }
}

void MetricsAdapter::SendSchedulerAlarm(const std::string &locationInfo)
{
    if (enabledInstruments_.find(YRInstrument::YR_SCHEDULER_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("scheduler alarm is not enabled");
        return;
    }
    if (metricsContext_.GetAttr("component_name") == "function_master") {
        YRLOG_DEBUG("{} sends scheduler alarm", metricsContext_.GetAttr("component_name"));
        alarmHandler_.SendSchedulerAlarm(locationInfo);
    }
}

void MetricsAdapter::SendTokenRotationFailureAlarm()
{
    if (enabledInstruments_.find(YRInstrument::YR_TOKEN_ROTATION_FAILURE_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("oidc token alarm is not enabled");
        return;
    }
    alarmHandler_.SendTokenRotationFailureAlarm();
}

void MetricsAdapter::SendS3Alarm()
{
    if (enabledInstruments_.find(YRInstrument::YR_S3_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("s3 alarm is not enabled");
        return;
    }
    alarmHandler_.SendS3Alarm();
}

void MetricsAdapter::SendPodAlarm(const std::string &podName, const std::string &cause)
{
    if (enabledInstruments_.find(YRInstrument::YR_POD_ALARM) == enabledInstruments_.end()) {
        YRLOG_DEBUG("pod alarm is not enabled");
        return;
    }
    alarmHandler_.SendPodAlarm(podName, cause);
}

void MetricsAdapter::InitObservableCounter(const struct MeterTitle &title, int interval, MetricsApi::CallbackPtr &cb,
                                           observability::sdk::metrics::InstrumentValueType observableType)
{
    if (observableInstrumentMap_.find(title.name) != observableInstrumentMap_.end()) {
        YRLOG_DEBUG("{} ObservableCounter exists, type({})", title.name, static_cast<uint32_t>(observableType));
        return;
    }
    auto provider = MetricsApi::Provider::GetMeterProvider();
    if (provider == nullptr) {
        return;
    }
    std::shared_ptr<MetricsApi::Meter> meter = provider->GetMeter("observable_instrument_meter");
    if (meter == nullptr) {
        return;
    }

    std::shared_ptr<MetricsApi::ObservableInstrument> observableInstrument;
    switch (observableType) {
        case observability::sdk::metrics::InstrumentValueType::UINT64:
            observableInstrument =
                meter->CreateUint64ObservableCounter(title.name, title.description, title.unit, interval, cb);
            break;

        // not support yet
        case observability::sdk::metrics::InstrumentValueType::INT64:
        case observability::sdk::metrics::InstrumentValueType::DOUBLE:
        default:
            // observe uint64 for default
            observableInstrument =
                meter->CreateUint64ObservableCounter(title.name, title.description, title.unit, interval, cb);
    }

    observableInstrumentMap_[title.name] = std::move(observableInstrument);
    YRLOG_DEBUG("InitObservableCounter for {}", title.name);
}

void MetricsAdapter::InitObservableGauge(const struct MeterTitle &title, int interval, MetricsApi::CallbackPtr &cb,
                                         observability::sdk::metrics::InstrumentValueType observableType)
{
    if (observableInstrumentMap_.find(title.name) != observableInstrumentMap_.end()) {
        YRLOG_DEBUG("{} ObservableGauge exists, type({})", title.name, static_cast<uint32_t>(observableType));
        return;
    }
    auto provider = MetricsApi::Provider::GetMeterProvider();
    if (provider == nullptr) {
        return;
    }

    std::shared_ptr<MetricsApi::Meter> meter = provider->GetMeter("observable_instrument_meter");
    if (meter == nullptr) {
        return;
    }

    std::shared_ptr<MetricsApi::ObservableInstrument> observableInstrument;
    switch (observableType) {
        case observability::sdk::metrics::InstrumentValueType::DOUBLE:
            observableInstrument =
                meter->CreateDoubleObservableGauge(title.name, title.description, title.unit, interval, cb);
            break;

        // not support yet
        case observability::sdk::metrics::InstrumentValueType::UINT64:
        case observability::sdk::metrics::InstrumentValueType::INT64:
        default:
            // observe double for default
            observableInstrument =
                meter->CreateDoubleObservableGauge(title.name, title.description, title.unit, interval, cb);
    }

    observableInstrumentMap_[title.name] = std::move(observableInstrument);
    YRLOG_DEBUG("InitObservableGauge for {}", title.name);
}

void MetricsAdapter::InitDoubleGauge(const struct MeterTitle &title)
{
    if (doubleGaugeMap_.find(title.name) != doubleGaugeMap_.end()) {
        return;
    }
    auto provider = MetricsApi::Provider::GetMeterProvider();
    if (provider == nullptr) {
        return;
    }
    std::shared_ptr<MetricsApi::Meter> meter = provider->GetMeter("gauge_meter");
    if (meter == nullptr) {
        return;
    }
    auto gauge = meter->CreateDoubleGauge(title.name, title.description, title.unit);
    doubleGaugeMap_[title.name] = std::move(gauge);
}

void MetricsAdapter::ReportGauge(const struct MeterTitle &title, struct MeterData &data)
{
    std::list<std::string> contextAttrs = { "node_id", "ip" };
    ReportDoubleGauge(title, data, contextAttrs);
}

std::vector<std::string> MetricsAdapter::ConvertNodeLabels(const NodeLabelsType &nodeLabels)
{
    std::vector<std::string> poolLabels;
    for (auto &keyIt : nodeLabels) {
        auto key = keyIt.first;
        for (auto &valueIt : keyIt.second) {
            if (valueIt.second > 0) {
                poolLabels.emplace_back(key + ":" + valueIt.first);
            }
        }
    }
    return poolLabels;
}

void MetricsAdapter::RegisterBillingInstanceRunningDuration()
{
    if (enabledInstruments_.find(YRInstrument::YR_INSTANCE_RUNNING_DURATION) == enabledInstruments_.end()) {
        YRLOG_DEBUG("Billing Instance Running Duration is not enabled");
        return;
    }
    MeterTitle meterTitle{ GetInstrumentDesc(YRInstrument::YR_INSTANCE_RUNNING_DURATION),
                           "Billing Instance Running Duration", "milliseconds" };
    MetricsApi::CallbackPtr cb =
        std::bind(&MetricsAdapter::CollectBillingInstanceRunningDuration, this, std::placeholders::_1);
    InitObservableCounter(meterTitle, INSTANCE_RUNNING_DURATION_COLLECT_INTERVAL, cb,
                          observability::sdk::metrics::InstrumentValueType::UINT64);
}

void MetricsAdapter::CollectBillingInstanceRunningDuration(MetricsApi::ObserveResult obRes)
{
    std::vector<std::pair<MetricsApi::MetricLabels, uint64_t>> vec;
    YRLOG_DEBUG("Collect billing instance size: {}, extra instance size {}",
                metricsContext_.GetBillingInstanceMap().size(), metricsContext_.GetExtraBillingInstanceMap().size());
    for (auto i : metricsContext_.GetBillingInstanceMap()) {
        auto data = BuildBillingInstanceRunningDurationData(i.first, i.second, false);
        if (data.second > 0) {
            vec.push_back(data);
        }
    }
    for (auto i : metricsContext_.GetExtraBillingInstanceMap()) {
        auto data = BuildBillingInstanceRunningDurationData(i.first, i.second, true);
        if (data.second > 0) {
            vec.push_back(data);
        }
    }
    if (std::holds_alternative<std::shared_ptr<MetricsApi::ObserveResultT<uint64_t>>>(obRes)) {
        std::get<std::shared_ptr<MetricsApi::ObserveResultT<uint64_t>>>(obRes)->Observe(vec);
    }
}

std::pair<MetricsApi::MetricLabels, uint64_t> MetricsAdapter::BuildBillingInstanceRunningDurationData(
    const std::string &instanceID, const BillingInstanceInfo &billingInstanceInfo, bool extraBilling)
{
    if (billingInstanceInfo.isSystemFunc) {
        YRLOG_DEBUG("{} system function can't bill", instanceID);
        return std::pair{ MetricsApi::MetricLabels{}, 0 };
    }

    auto billingFunctionOption = metricsContext_.GetBillingFunctionOption(instanceID);
    nlohmann::json poolLabelsJson = ConvertNodeLabels(billingFunctionOption.nodeLabels);

    long long reportTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (billingInstanceInfo.endTimeMillis > 0) {
        reportTimeMillis = billingInstanceInfo.endTimeMillis;
    }
    metrics::LabelType labelMap;
    try {
        labelMap = { { "instance_id", instanceID },
                    { "cpu_type", billingFunctionOption.cpuType },
                    { "init_ms", std::to_string(billingInstanceInfo.startTimeMillis) },
                    { "last_report_ms", std::to_string(billingInstanceInfo.lastReportTimeMillis) },
                    { "report_ms", std::to_string(reportTimeMillis) },
                    { "pool_label", poolLabelsJson.dump() } };
    } catch (std::exception &e) {
        YRLOG_ERROR("dump labelMap failed, error: {}", e.what());
    }
    for (auto ite : std::as_const(billingInstanceInfo.customCreateOption)) {
        labelMap[ite.first] = ite.second;
    }

    if (reportTimeMillis <= billingInstanceInfo.lastReportTimeMillis) {
        YRLOG_ERROR("{} billing instance has invalid reportTimeMillis: {}, lastReportTimeMillis: {}", instanceID,
                    reportTimeMillis, billingInstanceInfo.lastReportTimeMillis);
        return std::pair{ MetricsApi::MetricLabels{}, 0 };
    }
    uint64_t value = static_cast<uint64_t>(reportTimeMillis - billingInstanceInfo.lastReportTimeMillis);
    MetricsApi::MetricLabels labels;
    for (auto iter = labelMap.begin(); iter != labelMap.end(); ++iter) {
        labels.emplace_back(*iter);
    }
    // Terminated instance: clear info; Non-terminated instance, update last report time
    if (extraBilling) {
        metricsContext_.EraseExtraBillingInstanceItem(instanceID);
    } else {
        if (billingInstanceInfo.endTimeMillis > 0) {
            YRLOG_DEBUG("Terminated billing instance {}, start time {}, end time {}", instanceID,
                        billingInstanceInfo.startTimeMillis, billingInstanceInfo.endTimeMillis);
            metricsContext_.EraseBillingFunctionOptionItem(instanceID);
            metricsContext_.EraseBillingInstanceItem(instanceID);
        } else {
            metricsContext_.SetBillingInstanceReportTime(instanceID, reportTimeMillis);
        }
    }
    return std::pair{ labels, value };
}

void MetricsAdapter::RegisterPodResource()
{
    if (enabledInstruments_.find(YRInstrument::YR_POD_RESOURCE) == enabledInstruments_.end()) {
        YRLOG_WARN("billing pod resource is not enabled");
        return;
    }
    MeterTitle meterTitle{ GetInstrumentDesc(YRInstrument::YR_POD_RESOURCE), "Pod Resources", "milliseconds" };
    if (observableInstrumentMap_.find(meterTitle.name) != observableInstrumentMap_.end()) {
        YRLOG_DEBUG("pod resource is already running");
        return;
    }
    MetricsApi::CallbackPtr cb = std::bind(&MetricsAdapter::CollectPodResource, this, std::placeholders::_1);
    InitObservableGauge(meterTitle, POD_RESOURCE_COLLECT_INTERVAL, cb,
                        observability::sdk::metrics::InstrumentValueType::DOUBLE);
}

void MetricsAdapter::CollectPodResource(MetricsApi::ObserveResult obRes)
{
    std::vector<std::pair<MetricsApi::MetricLabels, double>> vec;
    auto podResourceMap = metricsContext_.GetPodResourceMap();
    YRLOG_DEBUG("collect pod resource size: {}", podResourceMap.size());
    for (const auto &res : podResourceMap) {
        vec.push_back(BuildPodResourceData(res.first, res.second));
    }

    if (std::holds_alternative<std::shared_ptr<MetricsApi::ObserveResultT<double>>>(obRes)) {
        std::get<std::shared_ptr<MetricsApi::ObserveResultT<double>>>(obRes)->Observe(vec);
    }
}

std::pair<MetricsApi::MetricLabels, double> MetricsAdapter::BuildPodResourceData(const std::string &agentID,
                                                                                 const PodResource &podResource)
{
    nlohmann::json poolLabelsJson = ConvertNodeLabels(podResource.nodeLabels);
    metrics::LabelType labelMap;
    try {
        labelMap = {
            { "allocatable_cpu", GetResourceScalar(podResource.allocatable, resource_view::CPU_RESOURCE_NAME) },
            { "used_cpu", GetResourceUsedScalar(podResource.capacity, podResource.allocatable,
                    resource_view::CPU_RESOURCE_NAME) },
            { "cpu_capacity", GetResourceScalar(podResource.capacity, resource_view::CPU_RESOURCE_NAME) },
            { "allocatable_memory", GetResourceScalar(podResource.allocatable, resource_view::MEMORY_RESOURCE_NAME) },
            { "used_memory", GetResourceUsedScalar(podResource.capacity, podResource.allocatable,
                    resource_view::MEMORY_RESOURCE_NAME) },
            { "memory_capacity", GetResourceScalar(podResource.capacity, resource_view::MEMORY_RESOURCE_NAME) },
            { "pool_label", poolLabelsJson.dump() },
            { "report_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count()) }
        };
    } catch (std::exception &e) {
        YRLOG_ERROR("dump labelMap failed, error: {}", e.what());
    }

    MetricsApi::MetricLabels labels;
    for (auto iter = labelMap.begin(); iter != labelMap.end(); ++iter) {
        labels.emplace_back(*iter);
    }
    return std::pair{ labels, 0 };
}

void MetricsAdapter::ReportBillingInvokeLatency(const std::string &requestID, uint32_t errCode,
                                                long long startTimeMillis, long long endTimeMillis)
{
    YRLOG_DEBUG("{}|report billing invoke latency, errCode: {}", requestID, errCode);
    if (enabledInstruments_.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments_.end()) {
        YRLOG_DEBUG("Billing Invoke Latency is not enabled");
        return;
    }

    auto billingInvokeOption = metricsContext_.GetBillingInvokeOption(requestID);
    YRLOG_DEBUG("billing invoke lantency of function: {}, instanceID: {}", billingInvokeOption.functionName,
                billingInvokeOption.instanceID);
    if (billingInvokeOption.functionName == "" ||
        SYSTEM_FUNCTION_NAME.find(billingInvokeOption.functionName) != SYSTEM_FUNCTION_NAME.end()) {
        YRLOG_WARN("function name {} can't bill", billingInvokeOption.functionName);
        return;
    }

    auto billingFunctionOption = metricsContext_.GetBillingFunctionOption(billingInvokeOption.instanceID);
    nlohmann::json poolLabelsJson = ConvertNodeLabels(billingFunctionOption.nodeLabels);
    try {
        YRLOG_DEBUG("metrics nodeLabels are: {} ", poolLabelsJson.dump());
    } catch (std::exception &e) {
        YRLOG_ERROR("dump poolLabelsJson failed, error: {}", e.what());
    }
    std::string subUrl = "/instanceId/" + billingInvokeOption.instanceID + "/requestId/" +  requestID;
    metrics::LabelType labels;
    try {
         labels = { { "request_id", requestID },
                  { "function_name", billingInvokeOption.functionName },
                  { "status_code", std::to_string(errCode) },
                  { "start_ms", std::to_string(startTimeMillis) },
                  { "end_ms", std::to_string(endTimeMillis) },
                  { "interval_ms", std::to_string(endTimeMillis - startTimeMillis) },
                  { "pool_label", poolLabelsJson.dump() },
                  { "cpu_type", billingFunctionOption.cpuType },
                  { "export_sub_url", subUrl} };
    } catch (std::exception &e) {
        YRLOG_ERROR("dump initConfigJson failed, error: {}", e.what());
    }

    for (auto &ite : std::as_const(billingInvokeOption.invokeOptions)) {
        labels[ite.first] = ite.second;
    }
    for (auto &ite : std::as_const(billingFunctionOption.schedulingExtensions)) {
        labels[ite.first] = ite.second;
    }

    struct MeterData meterData {
        static_cast<double>(endTimeMillis - startTimeMillis), labels
    };
    functionsystem::metrics::MeterTitle meterTitle{
        GetInstrumentDesc(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY), "", "milliseconds" };
    ReportDoubleGauge(meterTitle, meterData, {});
    metricsContext_.EraseBillingInvokeOptionItem(requestID);
}

void MetricsAdapter::ReportDoubleGauge(const struct MeterTitle &title, struct MeterData &data,
                                       const std::list<std::string> &contextAttrs)
{
    std::lock_guard<std::mutex> l(mutex_);
    InitDoubleGauge(title);
    if (doubleGaugeMap_.find(title.name) == doubleGaugeMap_.end()) {
        return;
    }

    observability::sdk::metrics::PointLabels labels;

    for (const auto &attr : contextAttrs) {
        if (data.labels.find(attr) == data.labels.end()) {
            data.labels[attr] = metricsContext_.GetAttr(attr);
        }
    }

    for (auto iter = data.labels.begin(); iter != data.labels.end(); ++iter) {
        labels.emplace_back(*iter);
    }

    auto it = doubleGaugeMap_.find(title.name);
    it->second->Set(data.value, labels);
}

void MetricsAdapter::TransformGaugeParam(const std::string &name, const std::string &description,
                                         const std::string &unit, const double value)
{
    MeterTitle meterTitle{ name, description, unit };
    MeterData meterData{ value, {} };
    ReportDoubleGauge(meterTitle, meterData, {});
}

void MetricsAdapter::ReportClusterSourceState(const std::shared_ptr<resource_view::ResourceUnit> &unit)
{
    RETURN_IF_NULL(unit);

    double capacityCPU =
        unit->capacity().resources().at(functionsystem::resource_view::CPU_RESOURCE_NAME).scalar().value();
    TransformGaugeParam("yr_cluster_cpu_capacity", "", "vmillicore", capacityCPU);

    double allocatableCPU =
        unit->allocatable().resources().at(functionsystem::resource_view::CPU_RESOURCE_NAME).scalar().value();
    TransformGaugeParam("yr_cluster_cpu_allocatable", "", "vmillicore", allocatableCPU);

    double capacityMemory =
        unit->capacity().resources().at(functionsystem::resource_view::MEMORY_RESOURCE_NAME).scalar().value();
    TransformGaugeParam("yr_cluster_memory_capacity", "", "mb", capacityMemory);

    double allocatableMemory =
        unit->allocatable().resources().at(functionsystem::resource_view::MEMORY_RESOURCE_NAME).scalar().value();
    TransformGaugeParam("yr_cluster_memory_allocatable", "", "mb", allocatableMemory);
}

void MetricsAdapter::SetContextAttr(const std::string &attr, const std::string &value)
{
    metricsContext_.SetAttr(attr, value);
}

std::string MetricsAdapter::GetContextValue(const std::string &attr) const
{
    return metricsContext_.GetAttr(attr);
}

MetricsContext &MetricsAdapter::GetMetricsContext()
{
    return metricsContext_;
}

AlarmHandler &MetricsAdapter::GetAlarmHandler()
{
  return alarmHandler_;
}

std::string MetricsAdapter::GetExportModeDesc(const MetricsSdk::ExportMode &mode)
{
    switch (mode) {
        case MetricsSdk::ExportMode::IMMEDIATELY:
            return IMMEDIATELY_EXPORT;
        case MetricsSdk::ExportMode::BATCH:
            return BATCH_EXPORT;
        default:
            return "unknown";
    }
}

std::string MetricsAdapter::GetResourceScalar(const resources::Resources &resource, const std::string &resType)
{
    if (!resource.resources().contains(resType)) {
        return "0";
    }

    return std::to_string(resource.resources().at(resType).scalar().value());
}

std::string MetricsAdapter::GetResourceUsedScalar(const resources::Resources &capacity,
                                                  const resources::Resources &allocatable, const std::string &resType)
{
    if (!allocatable.resources().contains(resType)) {
        return GetResourceScalar(capacity, resType);
    }
    return std::to_string(capacity.resources().at(resType).scalar().value()
                          - allocatable.resources().at(resType).scalar().value());
}

}  // namespace metrics
}  // namespace functionsystem