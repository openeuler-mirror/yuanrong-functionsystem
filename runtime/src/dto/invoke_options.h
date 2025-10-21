/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "src/dto/affinity.h"
#include "src/dto/device.h"
#include "src/libruntime/err_type.h"
#include "src/libruntime/stacktrace/stack_trace_info.h"
#include "src/libruntime/utils/constants.h"
#include "src/proto/libruntime.pb.h"

namespace YR {
namespace Libruntime {

enum class BundleAffinity : int {
    COMPACT,
    DISCRETE,
};

struct FunctionGroupOptions {
    int functionGroupSize{0};
    int bundleSize{0};
    BundleAffinity bundleAffinity{BundleAffinity::COMPACT};
    int timeout{NO_TIMEOUT};
    bool sameLifecycle = true;
};

struct ResourceGroupOptions {
    std::string resourceGroupName{""};
    int bundleIndex{-1};
};

struct RangeOptions {
    int timeout = NO_TIMEOUT;
};

struct InstanceRange {
    int min = DEFAULT_INSTANCE_RANGE_NUM;
    int max = DEFAULT_INSTANCE_RANGE_NUM;
    int step = DEFAULT_INSTANCE_RANGE_STEP;
    bool sameLifecycle = true;
    RangeOptions rangeOpts;
};

struct InstanceSession {
    std::string sessionID = "";
    int sessionTTL;
    int concurrency;
};

struct InvokeOptions {
    int cpu = 500;

    int memory = 500;

    std::unordered_map<std::string, float> customResources;

    std::unordered_map<std::string, std::string> customExtensions;

    std::unordered_map<std::string, std::string> createOptions;

    std::unordered_map<std::string, std::string> podLabels;

    std::vector<std::string> labels;

    std::unordered_map<std::string, std::string> affinity;

    std::list<std::shared_ptr<Affinity>> scheduleAffinities;

    size_t retryTimes = 0;

    std::function<bool(const ErrorInfo &errInfo)> retryChecker = nullptr;

    size_t priority = 0;

    int instancePriority = 0;

    std::vector<std::string> codePaths;

    std::string schedulerFunctionId;

    std::vector<std::string> schedulerInstanceIds;

    std::string traceId;

    Device device;

    int maxInvokeLatency = 5000;

    int minInstances = 0;

    int maxInstances = 0;

    int groupTimeout = -1;

    std::string groupName;

    bool needOrder = false;

    int64_t scheduleTimeoutMs = 30000;

    bool preemptedAllowed = false;

    int recoverRetryTimes = 0;

    int timeout = 0;

    int acquireTimeout = 0;

    bool trafficLimited;

    InstanceRange instanceRange;

    ResourceGroupOptions resourceGroupOpts;

    FunctionGroupOptions functionGroupOpts;

    std::unordered_map<std::string, std::string> envVars;

    bool isGetInstance = false;

    std::unordered_map<std::string, std::string> invokeLabels;

    std::unordered_map<std::string, std::string> aliasParams;

    std::shared_ptr<InstanceSession> instanceSession;

    std::string workingDir;
};

struct FunctionMeta {
    std::string appName;
    std::string moduleName;
    std::string funcName;
    std::string className;
    libruntime::LanguageType languageType;
    std::string codeId;     // transferring python code with data object, use codeId as key to it
    std::string signature;  // java function signature
    std::string poolLabel;
    libruntime::ApiType apiType;
    std::string functionId;
    std::optional<std::string> name;
    std::optional<std::string> ns;
    std::string initializerCodeId;
    bool isAsync = false;
    bool isGenerator = false;
    bool needOrder = false;
};

struct GroupOpts {
    std::string groupName;
    int timeout;
    bool sameLifecycle = true;
};

struct InstanceOptions {
    bool needOrder;
};

struct DoubleCounterData {
    std::string name;
    std::string description;
    std::string unit;
    std::unordered_map<std::string, std::string> labels;
    double value;
};

struct UInt64CounterData {
    std::string name;
    std::string description;
    std::string unit;
    std::unordered_map<std::string, std::string> labels;
    uint64_t value;
};

struct GaugeData {
    std::string name;
    std::string description;
    std::string unit;
    std::unordered_map<std::string, std::string> labels;
    double value;
};

enum class AlarmSeverity { OFF, INFO, MINOR, MAJOR, CRITICAL };

struct AlarmInfo {
    std::string alarmName;
    AlarmSeverity alarmSeverity = AlarmSeverity::OFF;
    std::string locationInfo;
    std::string cause;
    long startsAt = DEFAULT_ALARM_TIMESTAMP;
    long endsAt = DEFAULT_ALARM_TIMESTAMP;
    long timeout = DEFAULT_ALARM_TIMEOUT;
    std::unordered_map<std::string, std::string> customOptions;
};
}  // namespace Libruntime
}  // namespace YR
