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

#ifndef COMMON_UTILS_LABEL_AFFINITY_UTILS_H
#define COMMON_UTILS_LABEL_AFFINITY_UTILS_H

#include "common/proto/pb/posix_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/scheduler_framework/utils/score.h"

namespace functionsystem {
const int64_t REQUIRED_AFFINITY_PRIORITY_NOT_MET = -1;
const int64_t ZERO_SCORE = 0;

bool IsLabelInValues(const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
                     const std::string &key, const ::google::protobuf::RepeatedPtrField<std::string> &values);

bool IsMatchLabelExpression(const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
                            const affinity::LabelExpression &expression);

bool IsAffinityPriority(const affinity::Selector &selector);

bool RequiredFilter(const std::string &unitID, const affinity::Selector &selector,
                    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

bool RequiredAntiFilter(const std::string &unitID, const affinity::Selector &selector,
                        const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

int64_t AffinityScorer(const std::string &unitID, const affinity::Selector &selector,
                       const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

int64_t AntiAffinityScorer(const std::string &unitID, const affinity::Selector &selector,
                           const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

bool IsResourceRequiredAffinityPassed(
    const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

int64_t CalculateInstanceAffinityScore(
    const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

int64_t CalculateResourceAffinityScore(
    const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels);

}  // namespace functionsystem

#endif  // COMMON_UTILS_LABEL_AFFINITY_UTILS_H
