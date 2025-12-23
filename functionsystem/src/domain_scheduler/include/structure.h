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
#ifndef DOMAIN_SCHEDULER_STRUCTURE_H
#define DOMAIN_SCHEDULER_STRUCTURE_H
#include <string>

#include "meta_store_client/meta_store_client.h"
#include "common/resource_view/resource_view_actor.h"

namespace functionsystem::domain_scheduler {

struct DomainSchedulerParam {
    std::string identity;  // domain unique identity, using to register global
    std::string globalAddress;
    std::shared_ptr<MetaStoreClient> metaStoreClient;
    uint32_t heartbeatTimeoutMs;
    uint64_t pullResourceInterval;
    bool isScheduleTolerateAbnormal;
    uint16_t maxPriority = 0;
    bool enablePreemption = false;
    int32_t relaxed = -1;
    bool enableMetrics = true;
    bool enablePrintResourceView = false;
    std::string schedulePlugins = "";
    std::string aggregatedStrategy{"no_aggregate"}; // three options : no_aggregate, strictly, relaxed
    std::string componentName = "";
    bool enableVerticalScale = false;
};
}  // namespace functionsystem::domain_scheduler
#endif  // DOMAIN_SCHEDULER_STRUCTURE_H
