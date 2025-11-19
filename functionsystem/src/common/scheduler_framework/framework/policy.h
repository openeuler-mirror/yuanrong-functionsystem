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
#ifndef SCHEDULER_POLICY_PLUGIN_H
#define SCHEDULER_POLICY_PLUGIN_H

#include <string>
#include <unordered_map>

#include "common/resource_view/resource_type.h"
#include "common/scheduler_framework/utils/score.h"
#include "common/status/status.h"

namespace functionsystem::schedule_framework {

enum class PolicyType { FILTER_POLICY, SCORE_POLICY, BIND_POLICY, PRE_FILTER_POLICY, UNDEFINED_POLICY };

class ScheduleContext {
public:
    ScheduleContext() = default;
    virtual ~ScheduleContext() = default;

    std::set<std::string> unfeasiblesNode;
    void ClearUnfeasible()
    {
        unfeasiblesNode.clear();
    }
    bool CheckNodeFeasible(const std::string &id)
    {
        return unfeasiblesNode.find(id) == unfeasiblesNode.end();
    }
    void TagNodeUnfeasible(const std::string &id)
    {
        unfeasiblesNode.insert(id);
    }
};

class SchedulePolicyPlugin {
public:
    SchedulePolicyPlugin() = default;
    virtual ~SchedulePolicyPlugin() = default;
    virtual std::string GetPluginName() = 0;
    virtual PolicyType GetPluginType() = 0;
};

class PreFilterResult {
public:
    explicit PreFilterResult(Status status) : status_(status){};
    virtual ~PreFilterResult() = default;
    virtual bool empty() = 0;
    // check whether the current result reaches the last element.
    virtual bool end() = 0;
    // next element
    virtual void next() = 0;
    // pod/node name of the current result
    virtual const std::string &current() = 0;
    // to reset begin of PreFilterResult
    virtual void reset(const std::string &cur) {};
    Status status()
    {
        return status_;
    }

private:
    Status status_;
};

template <typename T>
class ProtoMapPreFilterResult : public PreFilterResult {
public:
    using MapType = google::protobuf::Map<std::string, T>;
    using MapIterator = typename MapType::const_iterator;

    ProtoMapPreFilterResult(const MapType &map, Status status)
        : PreFilterResult(status), map_(map), currentIt_(map.begin()), endIt_(map.end()), loopedEnd_(map.end()),
        needLooped_(false)
    {
    }

    ~ProtoMapPreFilterResult() override = default;

    bool empty() override
    {
        return map_.empty();
    }

    bool end() override
    {
        return currentIt_ == endIt_;
    }

    void next() override
    {
        ++currentIt_;
        if (needLooped_ && currentIt_ == map_.end()) {
            currentIt_ = map_.begin();
            endIt_ = loopedEnd_;
            needLooped_ = false;
        }
    }

    const std::string &current() override
    {
        if (end()) {
            return nullStr;
        }
        return currentIt_->first;
    }

    void reset(const std::string &cur) override
    {
        auto it = map_.find(cur);
        if (it == map_.end()) {
            return;
        }
        it++;
        if (it != map_.end()) {
            needLooped_ = true;
            currentIt_ = it;
            loopedEnd_ = it;
        }
    }

private:
    const MapType &map_;
    MapIterator currentIt_;
    MapIterator endIt_;
    MapIterator loopedEnd_;
    bool needLooped_;
    std::string nullStr;
};

class SetPreFilterResult : public PreFilterResult {
public:
    using SetType = std::set<std::string>;
    using SetIterator = SetType::const_iterator;

    SetPreFilterResult(const SetType &set, Status status)
        : PreFilterResult(status), set_(set), setIt_(set.begin()), setEnd_(set.end())
    {
    }

    const std::string &current() override
    {
        if (setIt_ != setEnd_) {
            return *setIt_;
        }
        return nullStr;  // return reference, can not return ""; directly
    }

    bool empty() override
    {
        return set_.empty();
    }

    void next() override
    {
        if (setIt_ != setEnd_) {
            ++setIt_;
        }
    }

    bool end() override
    {
        return setIt_ == setEnd_;
    }

private:
    const SetType &set_;
    SetIterator setIt_;
    SetIterator setEnd_;
    std::string nullStr;
};

class PreFilterPlugin : public SchedulePolicyPlugin {
public:
    PreFilterPlugin() = default;
    ~PreFilterPlugin() override = default;
    PolicyType GetPluginType() override
    {
        return PolicyType::PRE_FILTER_POLICY;
    }

    virtual std::shared_ptr<PreFilterResult> PreFilter(const std::shared_ptr<ScheduleContext> &ctx,
                                                       const resource_view::InstanceInfo &instance,
                                                       const resource_view::ResourceUnit &resourceUnit) = 0;

    virtual bool PrefilterMatched(const resource_view::InstanceInfo &instance)
    {
        return true;
    }
};

struct Filtered {
    Status status;
    // If a fatal error is returned, the scheduling cannot be continued.
    // while status is ok, isFatalErr would be ignored
    bool isFatalErr;
    // Indicates the number of requests that can be scheduled in the current pod or unit.
    // -1 means no limited, etc: while label affinity matched, no matter how many instances can be scheduled if the
    // resource is allowed.
    int32_t availableForRequest;
    // required resource or affinity info
    std::string required{ "" };
};

class FilterPlugin : public SchedulePolicyPlugin {
public:
    FilterPlugin() = default;
    ~FilterPlugin() override = default;
    PolicyType GetPluginType() override
    {
        return PolicyType::FILTER_POLICY;
    }

    /**
     * Determine whether a single resource unit meets requirements.
     * @param ctx: Scheduling context information, including resources that have been pre-allocate
     * @param instance: Instance meta information (including the CPU and memory required by the instance).
     * @param resourceUnit: Resource unit of a node/pod, including available resources and label information.
     * @return Status: Indicates the returned error information. The cause of the error must be specified.
     */
    virtual Filtered Filter(const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                            const resource_view::ResourceUnit &resourceUnit) = 0;
};

class ScorePlugin : public SchedulePolicyPlugin {
public:
    ScorePlugin() = default;
    ~ScorePlugin() override = default;
    PolicyType GetPluginType() override
    {
        return PolicyType::SCORE_POLICY;
    }
    /**
     * Calculate the scheduling score of a single schedulable unit.
     * @param ctx: Scheduling context information, including resources that have been pre-allocate
     * @param instance: Instance meta information (including the CPU and memory required by the instance).
     * @param resourceUnit: Resource unit of a node/pod, including available resources and label information.
     * @return NodeScore: Indicates the score of the node/pod in the plugin.
     */
    virtual NodeScore Score(const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                            const resource_view::ResourceUnit &resourceUnit) = 0;
};

}  // namespace functionsystem::schedule_framework
#endif
