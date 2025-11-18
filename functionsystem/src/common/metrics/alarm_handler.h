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

#ifndef FUNCTIONSYSTEM_ALARM_HANDLER_H
#define FUNCTIONSYSTEM_ALARM_HANDLER_H

#include <memory>

#include "metrics/api/alarm.h"
#include "metrics/api/provider.h"
#include "metrics_constants.h"

namespace functionsystem {
namespace metrics {
namespace MetricsApi = observability::api::metrics;

class AlarmHandler {
public:
    AlarmHandler() = default;
    ~AlarmHandler() noexcept = default;

    MetricsApi::AlarmSeverity GetAlarmLevel(const AlarmLevel level) const;

    void SendK8sAlarm(const std::string &locationInfo);
    void SendSchedulerAlarm(const std::string &locationInfo);
    void SendBackendStorageAlarm(const MetricsApi::AlarmInfo &etcdAlarmInfo, const std::string backend);
    void SendTokenRotationFailureAlarm();
    void SendS3Alarm();
    void SendPodAlarm(const std::string &podName, const std::string &cause);
    void SendElectionAlarm(const MetricsApi::AlarmInfo &electionAlarmInfo);

    [[maybe_unused]] std::unordered_map<std::string, std::shared_ptr<observability::api::metrics::Alarm>> GetAlarmMap()
    {
        return alarmMap_;
    };

private:
    std::mutex mutex_{};
    std::unordered_map<std::string, std::shared_ptr<observability::api::metrics::Alarm>> alarmMap_{};
    std::shared_ptr<MetricsApi::Alarm> InitAlarm(const std::string &alarmName, const std::string &alarmDesc);
};
}
}
#endif  // FUNCTIONSYSTEM_ALARM_HANDLER_H
