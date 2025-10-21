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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_RESOURCE_LABELS_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_RESOURCE_LABELS_COLLECTOR_H

#include "base_system_proc_collector.h"

namespace functionsystem::runtime_manager {

const std::string INIT_LABELS_ENV_KEY = "INIT_LABELS";
const std::string NODE_ID_LABEL_KEY = "NODE_ID";
const std::string HOST_IP_LABEL_KEY = "HOST_IP";

class ResourceLabelsCollector : public BaseMetricsCollector {
public:
    explicit ResourceLabelsCollector(const std::string &resourceLabelPath = "");
    ~ResourceLabelsCollector() override = default;
    std::string GenFilter() const override;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;

private:
    void GetInitLabelsFromEnv(std::unordered_map<std::string, std::string> &labelsMap);
    void GetNodeIDLabelsFromEnv(std::unordered_map<std::string, std::string> &labelsMap);
    void GetHostIPLabelsFromEnv(std::unordered_map<std::string, std::string> &labelsMap);
    void GetLabelsFromFile(std::unordered_map<std::string, std::string> &labelsMap);
    Metric initLabelsCache_;
    std::string resourceLabelPath_;
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_RESOURCE_LABELS_COLLECTOR_H

