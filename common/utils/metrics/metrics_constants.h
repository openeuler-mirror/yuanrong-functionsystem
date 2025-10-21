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

#ifndef FUNCTIONSYSTEM_METRICS_CONSTANT_H
#define FUNCTIONSYSTEM_METRICS_CONSTANT_H

#include <mutex>
#include <string>
#include <unordered_map>

namespace functionsystem {
namespace metrics {

const std::string UNKNOWN_INSTRUMENT_NAME = "unknown_instrument_name";
const std::string YR_INSTANCE_RUNNING_DURATION = "yr_instance_running_duration";
const std::string YR_APP_INSTANCE_BILLING_INVOKE_LATENCY = "yr_app_instance_billing_invoke_latency";
const std::string YR_METRICS_KEY = "YR_Metrics";
const std::string YR_POD_RESOURCE("yr_pod_resource");

// alarm
const std::string K8S_ALARM("yr_k8s_alarm");
const std::string SCHEDULER_ALARM("yr_proxy_alarm");
const std::string ETCD_ALARM("yr_etcd_alarm");
const std::string TOKEN_ROTATION_FAILURE_ALARM("yr_token_rotation_failure_alarm");
const std::string S3_ALARM("yr_obs_alarm");
const std::string POD_ALARM("yr_pod_alarm");
const std::string ELECTION_ALARM("yr_election_alarm");

const std::string FILE_EXPORTER("fileExporter");

const uint32_t SIZE_MEGA_BYTES = 1024 * 1024;  // 1 MB
const uint32_t INSTANCE_RUNNING_DURATION_COLLECT_INTERVAL = 15;  // unit:second
const uint32_t POD_RESOURCE_COLLECT_INTERVAL = 15;  // unit:second

const int TOKEN_ROTATION_FAILURE_TIMES_THRESHOLD = 3;

const uint32_t MONOPOLY_INSTANCE_COUNT = 1;

enum class YRInstrument {
    UNKNOWN_INSTRUMENT = 0,
    YR_INSTANCE_RUNNING_DURATION = 1,
    YR_APP_INSTANCE_BILLING_INVOKE_LATENCY = 2,
    YR_K8S_ALARM = 3,
    YR_SCHEDULER_ALARM = 4,
    YR_ETCD_ALARM = 5,
    YR_TOKEN_ROTATION_FAILURE_ALARM = 6,
    YR_S3_ALARM = 7,
    YR_POD_ALARM = 8,
    YR_POD_RESOURCE = 9,
    YR_ELECTION_ALARM = 10,
};

enum class AlarmLevel { OFF, NOTICE, INFO, MINOR, MAJOR, CRITICAL };

const std::unordered_map<std::string, YRInstrument> INSTRUMENT_DESC_2_ENUM = {
    { YR_INSTANCE_RUNNING_DURATION, YRInstrument::YR_INSTANCE_RUNNING_DURATION },
    { YR_APP_INSTANCE_BILLING_INVOKE_LATENCY, YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY },
    { K8S_ALARM, YRInstrument::YR_K8S_ALARM },
    { SCHEDULER_ALARM, YRInstrument::YR_SCHEDULER_ALARM },
    { ETCD_ALARM, YRInstrument::YR_ETCD_ALARM },
    { S3_ALARM, YRInstrument::YR_S3_ALARM },
    { POD_ALARM, YRInstrument::YR_POD_ALARM },
    { YR_POD_RESOURCE, YRInstrument::YR_POD_RESOURCE },
    { ELECTION_ALARM, YRInstrument::YR_ELECTION_ALARM },
};

const std::unordered_map<YRInstrument, std::string> ENUM_2_INSTRUMENT_DESC = {
    { YRInstrument::YR_INSTANCE_RUNNING_DURATION, YR_INSTANCE_RUNNING_DURATION },
    { YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY, YR_APP_INSTANCE_BILLING_INVOKE_LATENCY },
    { YRInstrument::YR_K8S_ALARM, K8S_ALARM },
    { YRInstrument::YR_SCHEDULER_ALARM, SCHEDULER_ALARM },
    { YRInstrument::YR_ETCD_ALARM, ETCD_ALARM },
    { YRInstrument::YR_S3_ALARM, S3_ALARM },
    { YRInstrument::YR_POD_ALARM, POD_ALARM },
    { YRInstrument::YR_POD_RESOURCE, YR_POD_RESOURCE },
    { YRInstrument::YR_ELECTION_ALARM, ELECTION_ALARM },
};
}
}

#endif // FUNCTIONSYSTEM_METRICS_CONSTANT_H
