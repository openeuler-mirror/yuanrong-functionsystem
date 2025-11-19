/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

#include "alarm_handler.h"

#include <nlohmann/json.hpp>

#include "common/logs/logging.h"
#include "metrics_constants.h"
#include "metrics_utils.h"

namespace functionsystem {
namespace metrics {

MetricsApi::AlarmSeverity AlarmHandler::GetAlarmLevel(const AlarmLevel level) const
{
    switch (level) {
        case AlarmLevel::OFF:
            return MetricsApi::AlarmSeverity::OFF;
        case AlarmLevel::INFO:
            return MetricsApi::AlarmSeverity::INFO;
        case AlarmLevel::MINOR:
            return MetricsApi::AlarmSeverity::MINOR;
        case AlarmLevel::MAJOR:
            return MetricsApi::AlarmSeverity::MAJOR;
        case AlarmLevel::CRITICAL:
            return MetricsApi::AlarmSeverity::CRITICAL;
        default:
            return MetricsApi::AlarmSeverity::MAJOR;
    }
}

void AlarmHandler::SendK8sAlarm(const std::string &locationInfo)
{
    auto k8sAlarm = InitAlarm(K8S_ALARM, "k8s alarm");
    if (k8sAlarm == nullptr) {
        return;
    }
    MetricsApi::AlarmInfo k8sAlarmInfo;
    k8sAlarmInfo.alarmName = K8S_ALARM;
    k8sAlarmInfo.alarmSeverity = MetricsApi::AlarmSeverity::CRITICAL;
    k8sAlarmInfo.startsAt = GetCurrentTimeInMilliSec();
    k8sAlarmInfo.locationInfo = locationInfo;
    if (!locationInfo.empty()) {
        k8sAlarmInfo.customOptions["resource_id"] = locationInfo;
    }
    k8sAlarm->Set(k8sAlarmInfo);
}

void AlarmHandler::SendSchedulerAlarm(const std::string &locationInfo)
{
    auto schedulerAlarm = InitAlarm(SCHEDULER_ALARM, "scheduler alarm");
    if (schedulerAlarm == nullptr) {
        return;
    }
    MetricsApi::AlarmInfo schedulerAlarmInfo;
    schedulerAlarmInfo.alarmName = SCHEDULER_ALARM;
    schedulerAlarmInfo.alarmSeverity = MetricsApi::AlarmSeverity::CRITICAL;
    schedulerAlarmInfo.startsAt = GetCurrentTimeInMilliSec();
    schedulerAlarmInfo.locationInfo = locationInfo;
    if (!locationInfo.empty()) {
        schedulerAlarmInfo.customOptions["resource_id"] = locationInfo;
    }
    schedulerAlarm->Set(schedulerAlarmInfo);
}

void AlarmHandler::SendBackendStorageAlarm(const MetricsApi::AlarmInfo &alarmInfo, const std::string backend)
{
    auto alarmName = backend == "Metastore" ? METASTORE_ALARM : ETCD_ALARM;
    auto etcdAlarm = InitAlarm(alarmName, backend + " alarm");
    if (etcdAlarm == nullptr) {
        return;
    }
    etcdAlarm->Set(alarmInfo);
}

void AlarmHandler::SendElectionAlarm(const MetricsApi::AlarmInfo &electionAlarmInfo)
{
    auto electionAlarm = InitAlarm(ELECTION_ALARM, "election alarm");
    if (electionAlarm == nullptr) {
        return;
    }
    electionAlarm->Set(electionAlarmInfo);
}

void AlarmHandler::SendTokenRotationFailureAlarm()
{
    auto tokenRotationFailureAlarm = InitAlarm(TOKEN_ROTATION_FAILURE_ALARM, "token rotation failure alarm");
    if (tokenRotationFailureAlarm == nullptr) {
        return;
    }
    MetricsApi::AlarmInfo tokenRotationFailureAlarmInfo;
    tokenRotationFailureAlarmInfo.alarmName = TOKEN_ROTATION_FAILURE_ALARM;
    tokenRotationFailureAlarmInfo.alarmSeverity = MetricsApi::AlarmSeverity::CRITICAL;
    tokenRotationFailureAlarmInfo.startsAt = GetCurrentTimeInMilliSec();
    nlohmann::json annotations;
    annotations["detail"] = "Failed to get temporaryAccessKey for 3 consecutive times";
    tokenRotationFailureAlarmInfo.customOptions["annotations"] = annotations.dump();
    tokenRotationFailureAlarm->Set(tokenRotationFailureAlarmInfo);
}

void AlarmHandler::SendS3Alarm()
{
    auto s3Alarm = InitAlarm(S3_ALARM, "s3 alarm");
    if (s3Alarm == nullptr) {
        return;
    }
    MetricsApi::AlarmInfo s3AlarmInfo;
    s3AlarmInfo.alarmName = S3_ALARM;
    s3AlarmInfo.alarmSeverity = MetricsApi::AlarmSeverity::CRITICAL;
    s3AlarmInfo.startsAt = GetCurrentTimeInMilliSec();
    s3Alarm->Set(s3AlarmInfo);
}

void AlarmHandler::SendPodAlarm(const std::string &podName, const std::string &cause)
{
    auto podAlarm = InitAlarm(POD_ALARM, "pod alarm");
    if (podAlarm == nullptr) {
        return;
    }
    MetricsApi::AlarmInfo podAlarmInfo;
    podAlarmInfo.alarmName = POD_ALARM;
    podAlarmInfo.alarmSeverity = MetricsApi::AlarmSeverity::CRITICAL;
    podAlarmInfo.startsAt = GetCurrentTimeInMilliSec();
    nlohmann::json annotations;
    annotations["cause"] = podName + " is abnormal because " + cause;
    podAlarmInfo.customOptions["annotations"] = annotations.dump();
    podAlarm->Set(podAlarmInfo);
}

std::shared_ptr<MetricsApi::Alarm> AlarmHandler::InitAlarm(const std::string &alarmName, const std::string &alarmDesc)
{
    std::lock_guard<std::mutex> l(mutex_);
    if (alarmMap_.find(alarmName) != alarmMap_.end()) {
        return alarmMap_.find(alarmName)->second;
    }
    auto provider = MetricsApi::Provider::GetMeterProvider();
    if (provider == nullptr) {
        YRLOG_ERROR("Metrics provider is null, failed to init {}", alarmName);
        return nullptr;
    }
    std::shared_ptr<MetricsApi::Meter> meter = provider->GetMeter("alarm_meter");
    if (meter == nullptr) {
        YRLOG_ERROR("Metrics meter is null, failed to init {}", alarmName);
        return nullptr;
    }
    auto alarm = meter->CreateAlarm(alarmName, alarmDesc);
    alarmMap_[alarmName] = std::move(alarm);
    return alarmMap_[alarmName];
}
}
}
