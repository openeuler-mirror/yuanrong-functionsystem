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

#ifndef COMMON_SCHEDULE_PLUGIN_AFFINITY_UTILS_H
#define COMMON_SCHEDULE_PLUGIN_AFFINITY_UTILS_H

#include "proto/pb/posix_pb.h"
#include "resource_type.h"
#include "common/scheduler_framework/utils/score.h"

namespace functionsystem::schedule_plugin {
const int64_t ZERO_SCORE = 0;

bool RequiredAffinityFilter(const std::string &unitID, const affinity::Selector &selector,
                            const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

bool RequiredAntiAffinityFilter(const std::string &unitID, const affinity::Selector &selector,
                                const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

int64_t AffinityScorer(const std::string &unitID, const affinity::Selector &selector,
                       const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

int64_t AntiAffinityScorer(const std::string &unitID, const affinity::Selector &selector,
                           const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

bool IsNodeAffinityScope(const resource_view::InstanceInfo &instance);

bool NeedAffinityScorer(const resource_view::InstanceInfo &instance);

bool NeedOptimalAffinityCheck(bool isRelaxed, bool isTopdownScheduling);

bool IsAffinityPriority(const affinity::Selector &selector);

}  // namespace functionsystem::schedule_plugin

#endif  // COMMON_SCHEDULE_PLUGIN_AFFINITY_UTILS_H
