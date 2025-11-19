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

#ifndef DOMAIN_SCHEDULER_SCHEDULER_PREALLOCATED_CONTEXT_H
#define DOMAIN_SCHEDULER_SCHEDULER_PREALLOCATED_CONTEXT_H

#include <set>
#include <unordered_map>

#include "common/resource_view/resource_type.h"
#include "common/resource_view/resource_tool.h"
#include "common/scheduler_framework/framework/framework.h"
#include "common/constants/constants.h"

namespace functionsystem::schedule_framework {

struct UnitResource {
    resource_view::Resources resource;
};

struct PodInfo {
    explicit PodInfo(int32_t monoNum = 0, int32_t sharedNum = 0) : monoNum(monoNum), sharedNum(sharedNum)
    {
    }
    explicit PodInfo(const resource_view::BucketInfo &bucketInfo)
        : monoNum(bucketInfo.monopolynum()), sharedNum(bucketInfo.sharednum())
    {
    }

    int32_t monoNum;
    int32_t sharedNum;
};

struct PodSpec {
    PodSpec(const std::string &proportion, const std::string &mem) : proportion(proportion), mem(mem)
    {
    }
    bool operator==(const PodSpec &podSpec) const
    {
        return proportion == podSpec.proportion && mem == podSpec.mem;
    }

    std::string proportion;
    std::string mem;
};

struct PodSpecScore {
    PodSpecScore(double capacityScore, double angleScore) : capacityScore(capacityScore), angleScore(angleScore)
    {
    }
    double capacityScore;
    double angleScore;
};

struct PodSpecHash {
    size_t operator()(const PodSpec &p) const
    {
        return std::hash<std::string>()(p.proportion + p.mem);
    }
};

struct NodeInfos {
    std::vector<std::pair<std::shared_ptr<PodSpec>, PodInfo>> podSpecWithInfo;
    std::map<int64_t, std::shared_ptr<PodSpec>> scoreWithPodSpec;
    std::shared_ptr<PodSpec> selectPodSpec;
    bool selectPodType{ false };  // false: mono, true: shared
};

struct PreAllocatedContext : public schedule_framework::ScheduleContext {
    resource_view::SCHEDULER_LEVEL schedulerLevel;

    std::unordered_map<std::string, UnitResource> allocated;
    std::set<std::string> conflictNodes;

    // key: instanceID, value: PodSpec
    std::unordered_map<std::string, std::vector<std::shared_ptr<PodSpec>>> instanceFeasiblePodSpec;
    // key: instanceID, value: function_agent selected in PRE_ALLOCATION
    std::unordered_map<std::string, std::string> preAllocatedSelectedFunctionAgentMap;
    // key: function_agent selected in PRE_ALLOCATION
    std::set<std::string> preAllocatedSelectedFunctionAgentSet;
    // key: requestID, value: (key: childNodeId, value: NodeInfo)
    std::unordered_map<std::string, std::unordered_map<std::string, NodeInfos>> instanceFeasibleNodeWithInfo;

    // key: plugin name
    ::google::protobuf::Map<std::string, messages::PluginContext> *pluginCtx;

    // key: unitID value: allocated instance label
    std::unordered_map<std::string, ::google::protobuf::Map<std::string, resource_view::ValueCounter>> allocatedLabels;

    // key: requestID value:(key: unitID value: default plugin score)
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> requestDefaultScores;

    // key: localId value: all instance label
    std::unordered_map<std::string, ::google::protobuf::Map<std::string, resource_view::ValueCounter>> allLocalLabels;

    ::google::protobuf::Map<std::string, resource_view::ValueCounter>* allLabels;

    PreAllocatedContext() = default;
    ~PreAllocatedContext() override = default;
};

inline void ClearContext(::google::protobuf::Map<std::string, messages::PluginContext> &pluginCtx)
{
    pluginCtx[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->mutable_scheduledresult()->clear();
    pluginCtx[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->mutable_scheduledscore()->clear();
    pluginCtx[DEFAULT_FILTER_PLUGIN].mutable_defaultctx()->mutable_filterctx()->clear();
    pluginCtx[GROUP_SCHEDULE_CONTEXT].mutable_groupschedctx()->clear_reserved();
}

inline void CopyPluginContext(::google::protobuf::Map<std::string, messages::PluginContext> &out,
                              ::google::protobuf::Map<std::string, messages::PluginContext> &in)
{
    out[LABEL_AFFINITY_PLUGIN] = in[LABEL_AFFINITY_PLUGIN];
    out[LABEL_AFFINITY_PLUGIN] = in[LABEL_AFFINITY_PLUGIN];
    out[DEFAULT_FILTER_PLUGIN] = in[DEFAULT_FILTER_PLUGIN];
}

}  // namespace functionsystem::schedule_framework

#endif  // DOMAIN_SCHEDULER_SCHEDULER_PREALLOCATED_CONTEXT_H
