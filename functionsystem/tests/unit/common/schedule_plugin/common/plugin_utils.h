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

#ifndef FUNCTIONSYSTEM_PLUGIN_UTILS_H
#define FUNCTIONSYSTEM_PLUGIN_UTILS_H
#include "common/resource_view/view_utils.h"

namespace functionsystem::test::schedule_plugin {
using std::pair;
using std::string;
using std::vector;

inline resource_view::BucketInfo GetBucketInfo(int32_t monoNum, int32_t sharedNum)
{
    resource_view::BucketInfo bucketInfo;
    bucketInfo.set_monopolynum(monoNum);
    bucketInfo.set_sharednum(sharedNum);
    return bucketInfo;
}

inline resource_view::Bucket GetBucket(resource_view::BucketInfo &&bucketInfo,
                                       const vector<pair<string, resource_view::BucketInfo>> &childNode)
{
    resource_view::Bucket bucket;
    (*bucket.mutable_total()) = bucketInfo;

    auto allocatable(bucket.mutable_allocatable());
    for (const auto &node : childNode) {
        allocatable->insert({ node.first, node.second });
    }

    return bucket;
}

inline resource_view::BucketIndex GetBucketIndex(const vector<pair<string, resource_view::Bucket>> &indexes)
{
    resource_view::BucketIndex bucketIndex;
    auto buckets(bucketIndex.mutable_buckets());

    for (const auto &index : indexes) {
        buckets->insert({ index.first, index.second });
    }

    return bucketIndex;
}

inline resource_view::Bucket GetAgentBucket(int monopolyNum, int sharedNum)
{
    int agentIndex = 0;
    vector<pair<string, resource_view::BucketInfo>> bucketInfos;
    for (int i = 0; i < monopolyNum; i++) {
        bucketInfos.push_back({ "agent" + std::to_string(agentIndex), GetBucketInfo(1, 0) });
        agentIndex++;
    }
    for (int j = 0; j < sharedNum; j++) {
        bucketInfos.push_back({ "agent" + std::to_string(agentIndex), GetBucketInfo(0, 0) });
        agentIndex++;
    }
    return GetBucket(GetBucketInfo(monopolyNum, sharedNum), bucketInfos);
}

inline resource_view::ResourceUnit GetNewDomainResourceUnit()
{
    resource_view::ResourceUnit unit;
    unit.set_id("DomainScheduler");
    auto bucketIndexes = unit.mutable_bucketindexs();

    vector<pair<string, resource_view::Bucket>> buckets0 = { std::make_pair("512.000000", GetAgentBucket(15, 6)),
                                                             std::make_pair("1024.000000", GetAgentBucket(15, 3)),
                                                             std::make_pair("2048.000000", GetAgentBucket(15, 1)),
                                                             std::make_pair("4096.000000", GetAgentBucket(15, 0)),
                                                             std::make_pair("32768.000000", GetAgentBucket(6, 4)) };

    vector<pair<string, resource_view::Bucket>> buckets1 = { std::make_pair("32000.000000", GetAgentBucket(6, 4)),
                                                             std::make_pair("42000.000000", GetAgentBucket(6, 4)) };

    vector<pair<string, resource_view::Bucket>> buckets2 = { std::make_pair("36000.000000", GetAgentBucket(6, 4)),
                                                             std::make_pair("42000.000000", GetAgentBucket(6, 4)) };

    bucketIndexes->insert({ "1.024000", GetBucketIndex(buckets0) });
    bucketIndexes->insert({ "2.048000", GetBucketIndex(buckets0) });
    bucketIndexes->insert({ "2.666667", GetBucketIndex(buckets1) });
    bucketIndexes->insert({ "2.000000", GetBucketIndex(buckets2) });

    return unit;
}

inline resource_view::ResourceUnit GetNewLocalResourceUnit(bool needFrag = true, bool needBucketIndex = true,
                                                           bool needBucket = true, int monopolyNum = 1)
{
    resource_view::ResourceUnit unit;
    unit.set_id("LocalScheduler");
    auto bucketIndexes = unit.mutable_bucketindexs();
    vector<pair<string, resource_view::BucketInfo>> bucketInfos;
    int cnt = 5;
    int totalNum = 0;
    auto frag = unit.mutable_fragment();
    if (needFrag) {
        for (int i = 0; i < cnt; i++) {
            resource_view::ResourceUnit agent = functionsystem::test::view_utils::Get1DResourceUnit();
            bucketInfos.push_back({ agent.id(), GetBucketInfo(monopolyNum, 0) });
            (*frag)[agent.id()] = agent;
            totalNum += monopolyNum;
        }
    }
    vector<pair<string, resource_view::Bucket>> buckets;
    if (needBucket) {
        buckets = { std::make_pair("512.000000", GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)),
                    std::make_pair("1024.000000", GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)),
                    std::make_pair("2048.000000", GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)),
                    std::make_pair("4096.000000", GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)) };
    } else {
        buckets = { std::make_pair("2048.000000", GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)),
                    std::make_pair("4096.000000", GetBucket(GetBucketInfo(totalNum, 0), bucketInfos)) };
    }

    if (needBucketIndex) {
        bucketIndexes->insert({ "1.024000", GetBucketIndex(buckets) });
        bucketIndexes->insert({ "2.048000", GetBucketIndex(buckets) });
    } else {
        bucketIndexes->insert({ "2.048000", GetBucketIndex(buckets) });
    }

    return unit;
}

inline resource_view::Resource GetResource(const string &name, double val)
{
    resource_view::Resource res;
    res.set_name(name);
    res.set_type(resources::Value_Type_SCALAR);
    auto scalar = res.mutable_scalar();
    scalar->set_value(val);
    return res;
}

inline resource_view::Resources GetResources(double memVal, double cpuVal)
{
    resource_view::Resources rs;
    rs.mutable_resources()->insert(
        { view_utils::RESOURCE_MEM_NAME, GetResource(view_utils::RESOURCE_MEM_NAME, memVal) });
    rs.mutable_resources()->insert(
        { view_utils::RESOURCE_CPU_NAME, GetResource(view_utils::RESOURCE_CPU_NAME, cpuVal) });
    return rs;
}

inline resource_view::InstanceInfo GetInstance(const string &instanceID, const string &policy, double memVal,
                                               double cpuVal)
{
    resource_view::InstanceInfo ins;
    ins.set_instanceid(instanceID);
    ins.set_requestid(instanceID);
    ins.mutable_scheduleoption()->set_schedpolicyname(policy);
    (*ins.mutable_resources()) = GetResources(memVal, cpuVal);

    return ins;
}

inline resource_view::ResourceUnit GetAgentResourceUnit(double cpu, double mem, int monoNum)
{
    resource_view::ResourceUnit unit;
    auto id = "AgentID_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    unit.set_id(id);
    (*unit.mutable_capacity()) = view_utils::GetCpuMemNpuResourcesWithValue(cpu, mem);     // cpu mem
    (*unit.mutable_allocatable()) = view_utils::GetCpuMemNpuResourcesWithValue(cpu, mem);  // cpu mem

    auto proportionStr = std::to_string(mem / cpu);
    auto memStr = std::to_string(mem);
    auto bucketIndexes = unit.mutable_bucketindexs();

    vector<pair<string, resource_view::BucketInfo>> bucketInfos;  // construct Info
    bucketInfos.push_back({ unit.id(), GetBucketInfo(monoNum, 0) });

    vector<pair<string, resource_view::Bucket>> buckets = {
        std::make_pair(memStr, GetBucket(GetBucketInfo(monoNum, 0), bucketInfos))  // construct Bucket
    };

    resource_view::Bucket bucket;  // construct Bucket with monoNum for Info total and map<string, Info> allocatable
    (*bucket.mutable_total()) = GetBucketInfo(monoNum, 0);
    bucket.mutable_allocatable()->insert({ unit.id(), GetBucketInfo(monoNum, 0) });

    resource_view::BucketIndex bucketIndex;  // construct unit BucketIndex for map<string, Bucket> buckets
    bucketIndex.mutable_buckets()->insert({ memStr, bucket });

    bucketIndexes->insert({ proportionStr, bucketIndex });  // construct unit for map<string, BucketIndex> bucketIndexs

    return unit;
}

inline resources::Value::Counter GetCounter(const std::string &value, const uint64_t &cnt)
{
    resources::Value::Counter cnter;
    (*cnter.mutable_items())[value] = cnt;
    return cnter;
}

inline resource_view::ResourceUnit NewResourceUnit(const std::string &name,
                                                   const std::unordered_map<std::string, std::string> labels)
{
    auto unit = resource_view::ResourceUnit();
    unit.set_id(name);
    for (auto [key, value] : labels) {
        (*unit.mutable_nodelabels())[key] = GetCounter(value, 1);
    }
    return unit;
}

inline void AddLabelsToUnit(resource_view::ResourceUnit &unit, resource_view::ResourceUnit &frag,
                            const std::unordered_map<std::string, std::string> labels)
{
    for (auto [key, value] : labels) {
        // Frag is a tag overlay with a value of 1.
        (*frag.mutable_nodelabels())[key] = GetCounter(value, 1);
        (*((*unit.mutable_fragment())[frag.id()].mutable_nodelabels()))[key] = GetCounter(value, 1);
        // Unit labels needs to be accumulated.
        (*unit.mutable_nodelabels())[key] = (*unit.mutable_nodelabels())[key] + GetCounter(value, 1);
    }
}

inline void AddFragmentToUnit(resource_view::ResourceUnit &unit, const resource_view::ResourceUnit &frag)
{
    (*unit.mutable_fragment())[frag.id()] = frag;
    for (auto [key, counter]: frag.nodelabels()) {
        (*unit.mutable_nodelabels())[key] = (*unit.mutable_nodelabels())[key] + counter;
    }
}

}  // namespace functionsystem::test::schedule_plugin
#endif  // FUNCTIONSYSTEM_PLUGIN_UTILS_H
