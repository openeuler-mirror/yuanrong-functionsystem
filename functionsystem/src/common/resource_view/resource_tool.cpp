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

#include "resource_tool.h"

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "scala_resource_tool.h"
#include "vectors_resource_tool.h"

namespace functionsystem::resource_view {

const int32_t THOUSAND_INT = 1000;
const double THOUSAND_DOUBLE = 1000.0;

const std::unordered_map<ValueType, ValueToStringFunc> GLOBAL_VALUE_TO_STRING_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueToString },
    { ValueType::Value_Type_VECTORS, VectorsValueToString }
};

const std::unordered_map<ValueType, ValueValidateFunc> GLOBAL_VALUE_VALIDATE_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueValidate },
    { ValueType::Value_Type_VECTORS, VectorsValueValidate }
};

const std::unordered_map<ValueType, ValueValidateFunc> GLOBAL_VALUE_IS_EMPTY_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueIsEmpty },
    { ValueType::Value_Type_VECTORS, VectorsValueIsEmpty }
};

const std::unordered_map<ValueType, ValueEqualFunc> GLOBAL_VALUE_IS_EQUAL_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueIsEqual },
    { ValueType::Value_Type_VECTORS, VectorsValueIsEqual }
};

const std::unordered_map<ValueType, ValueAddFunc> GLOBAL_VALUE_ADD_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueAdd },
    { ValueType::Value_Type_VECTORS, VectorsValueAdd } };

const std::unordered_map<ValueType, ValueSubFunc> GLOBAL_VALUE_SUB_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueSub },
    { ValueType::Value_Type_VECTORS, VectorsValueSub } };

const std::unordered_map<ValueType, ValueLessFunc> GLOBAL_VALUE_LESS_FUNCS = {
    { ValueType::Value_Type_SCALAR, ScalaValueLess },
    { ValueType::Value_Type_VECTORS, VectorsValueLess }
};
}  // namespace functionsystem::resource_view

namespace functionsystem {

using namespace resource_view;

bool operator<=(const Resource &l, const Resource &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));
    ASSERT_FS(l.name() == r.name() && l.type() == r.type());

    if (l == r) {
        return true;
    }

    auto it = GLOBAL_VALUE_LESS_FUNCS.find(l.type());
    ASSERT_FS(it != GLOBAL_VALUE_LESS_FUNCS.end());

    return it->second(l, r);
}

bool operator==(const Resource &l, const Resource &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));
    ASSERT_FS(l.name() == r.name() && l.type() == r.type());

    auto it = GLOBAL_VALUE_IS_EQUAL_FUNCS.find(l.type());
    ASSERT_FS(it != GLOBAL_VALUE_IS_EQUAL_FUNCS.end());

    return it->second(l, r);
}
bool operator!=(const Resource &l, const Resource &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));
    ASSERT_FS(l.name() == r.name() && l.type() == r.type());

    return !(l == r);
}

Resource operator+(const Resource &l, const Resource &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));
    ASSERT_FS(l.name() == r.name() && l.type() == r.type());

    auto it = GLOBAL_VALUE_ADD_FUNCS.find(l.type());
    ASSERT_FS(it != GLOBAL_VALUE_ADD_FUNCS.end());

    return it->second(l, r);
}

Resource operator-(const Resource &l, const Resource &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));
    ASSERT_FS(l.name() == r.name() && l.type() == r.type());

    auto it = GLOBAL_VALUE_SUB_FUNCS.find(l.type());
    ASSERT_FS(it != GLOBAL_VALUE_SUB_FUNCS.end());

    return it->second(l, r);
}

bool operator<=(const Resources &l, const Resources &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));

    if (l.resources().size() > r.resources().size()) {
        return false;
    }

    for (auto it1 : l.resources()) {
        auto it2 = r.resources().find(it1.first);
        if (it2 == r.resources().end()) {
            return false;
        } else {
            if (it1.second <= it2->second) {
                continue;
            } else {
                return false;
            }
        }
    }
    return true;
}

bool operator>(const Resources &l, const Resources &r)
{
    return !(l <= r);
}

bool operator==(const Resources &l, const Resources &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));

    if (l.resources().size() != r.resources().size()) {
        return false;
    }

    for (auto it1 : l.resources()) {
        auto it2 = r.resources().find(it1.first);
        if (it2 == r.resources().end()) {
            return false;
        } else {
            if (it1.second != it2->second) {
                return false;
            }
        }
    }
    return true;
}

bool operator!=(const Resources &l, const Resources &r)
{
    ASSERT_FS(IsValid(l) && IsValid(r));

    return !(l == r);
}

Resources operator+(const Resources &left, const Resources &right)
{
    ASSERT_FS(IsValid(left) && IsValid(right));

    Resources sum = left;
    for (auto it1 : right.resources()) {
        auto it2 = left.resources().find(it1.first);
        if (it2 == left.resources().end()) {
            (*sum.mutable_resources())[it1.first] = it1.second;
        } else {
            (*sum.mutable_resources())[it1.first] = (*sum.mutable_resources())[it1.first] + it1.second;
        }
    }
    return sum;
}

Resources operator-(const Resources &left, const Resources &right)
{
    ASSERT_FS(IsValid(left) && IsValid(right));

    Resources sub = left;
    for (auto it1 : right.resources()) {
        auto it2 = left.resources().find(it1.first);
        if (it2 == left.resources().end()) {
            YRLOG_WARN("have not enough resources to do subtraction, resource name = {}.", it1.first);
            continue;
        } else {
            sub.mutable_resources()->at(it1.first) = sub.mutable_resources()->at(it1.first) - it1.second;
        }
    }
    return sub;
}

// Add two counters will works like, the order doesn't matter
//   {"A": 3, "B": 2        , "D": 1}
// + {"A": 1,         "C": 4, "D": 1}
// = {"A": 4, "B": 2, "C": 4, "D": 2}
resources::Value::Counter operator+(const resources::Value::Counter &l, const resources::Value::Counter &r)
{
    resources::Value::Counter sum = l;
    for (auto itr : r.items()) {
        auto itl = l.items().find(itr.first);
        if (itl == l.items().end()) {
            // contain in right, but not contained in left
            (*sum.mutable_items())[itr.first] = itr.second;
        } else {
            // contained both sides, add values together
            (*sum.mutable_items())[itr.first] += itr.second;
        }
    }
    return sum;
}

// Sub two counters will works like
//   {"A": 3, "B": 2,         "D": 2}
// - {"A": 1,         "C": 4, "D": 2}
// = {"A": 2, "B": 2,               }
resources::Value::Counter operator-(const resources::Value::Counter &l, const resources::Value::Counter &r)
{
    resources::Value::Counter sub = l;
    for (auto itr : r.items()) {
        auto itl = l.items().find(itr.first);
        // contained both side
        if (itl != l.items().end()) {
            // if larger, then sub is valid, else should be erased
            if (itl->second > itr.second) {
                (*sub.mutable_items())[itr.first] -= itr.second;
            } else {
                (void)(*sub.mutable_items()).erase(itr.first);
            }
        }
    }
    return sub;
}

// The sum of two mapSet will contains all keys of both left and right
// and the value would be the sum of both side values
MapCounter operator+(const MapCounter &l, const MapCounter &r)
{
    MapCounter sum = l;
    for (auto itr : r) {
        auto itl = l.find(itr.first);
        if (itl == l.end()) {
            // contained right side only, add key to left
            sum[itr.first] = itr.second;
        } else {
            // contained both sides, add together
            sum[itr.first] = itl->second + itr.second;
        }
    }
    return sum;
}

MapCounter operator-(const MapCounter &l, const MapCounter &r)
{
    MapCounter sub = l;
    for (auto itr : r) {
        auto itl = l.find(itr.first);
        if (itl != l.end()) {
            sub[itr.first] = sub[itr.first] - itr.second;
            if (sub[itr.first].items().size() == 0) {
                (void)sub.erase(itr.first);
            }
        }
    }
    return sub;
}

MapCounter ToLabelKVs(const ::google::protobuf::Map<std::string, std::string> &labels)
{
    MapCounter results;
    for (auto &[key, value] : labels) {
        MapCounter result;
        resources::Value::Counter defaultCnt;
        (void)defaultCnt.mutable_items()->insert({ value, 1 });
        result[key] = defaultCnt;
        results = results + result;
    }
    return results;
}

MapCounter ToLabelKV(const std::string &label)
{
    const int32_t maxToken = 2;
    // Split would not return empty vector
    auto kvs = litebus::strings::Split(label, ":", maxToken);
    std::string value = "";
    std::string key = kvs[0];
    if (kvs.size() > 1) {
        value = kvs[1];
    }
    resources::Value::Counter defaultCnt;
    (void)defaultCnt.mutable_items()->insert({ value, 1 });
    MapCounter result;
    result[key] = defaultCnt;
    return result;
}

MapCounter ToLabelKVs(
    const ::google::protobuf::RepeatedPtrField<std::string> &labels)
{
    MapCounter result;
    for (const auto &label : labels) {
        result = result + ToLabelKV(label);
    }
    return result;
}

resource_view::Resources BuildResources(int64_t cpuVal, int64_t memVal)
{
    Resource resourceCPU;
    resourceCPU.set_name(CPU_RESOURCE_NAME);
    resourceCPU.set_type(ValueType::Value_Type_SCALAR);
    resourceCPU.mutable_scalar()->set_value(cpuVal);

    Resource resourceMemory;
    resourceMemory.set_name(MEMORY_RESOURCE_NAME);
    resourceMemory.set_type(ValueType::Value_Type_SCALAR);
    resourceMemory.mutable_scalar()->set_value(memVal);

    resource_view::Resources resources;
    auto resourcesMap = resources.mutable_resources();
    (*resourcesMap)[CPU_RESOURCE_NAME] = resourceCPU;
    (*resourcesMap)[MEMORY_RESOURCE_NAME] = resourceMemory;
    return resources;
}

void DeleteLabel(const resources::InstanceInfo &instInfo,
                 ::google::protobuf::Map<std::string, resource_view::ValueCounter> &nodeLabels)
{
    for (const auto &label : instInfo.labels()) {
        resources::Value::Counter cnter;
        (void)cnter.mutable_items()->insert({ label, 1 });
        if (nodeLabels.find(AFFINITY_SCHEDULE_LABELS) != nodeLabels.end()) {
            nodeLabels[AFFINITY_SCHEDULE_LABELS] = nodeLabels[AFFINITY_SCHEDULE_LABELS] - cnter;
        }
        nodeLabels = nodeLabels - ToLabelKV(label);
    }
    return;
}

resource_view::Bucket *GetBucketInfo(const resource_view::Resources &resources, resources::ResourceUnit &view)
{
    if (!HasValidCPU(resources) || !HasValidMemory(resources)) {
        return nullptr;
    }
    auto cpu = resources.resources().at(CPU_RESOURCE_NAME).scalar().value();
    auto mem = resources.resources().at(MEMORY_RESOURCE_NAME).scalar().value();
    if (abs(cpu) < EPSINON) {
        return nullptr;
    }
    auto proportion = mem / cpu;
    auto &bucketIndex = (*view.mutable_bucketindexs())[std::to_string(proportion)];
    auto &bucket = (*bucketIndex.mutable_buckets())[std::to_string(mem)];
    return &bucket;
}

void UpdateBucketInfoDelInstance(const resources::InstanceInfo &instance, const resource_view::Resources &resources,
                                 const int instanceSize, resources::ResourceUnit &view)
{
    auto bucket = GetBucketInfo(resources, view);
    if (bucket == nullptr) {
        YRLOG_WARN("invalid allocatable {} while delete instance {} from resource view.", resources.ShortDebugString(),
                   instance.instanceid());
        return;
    }
    auto &info = (*bucket->mutable_allocatable())[instance.unitid()];
    auto &total = *bucket->mutable_total();
    // If an instance's schedule policy is monopoly, it would be scheduled to a new pod
    // While deleting the instance, the pod where the instance was scheduled to is being deleted
    // To avoid scheduling a new instance to the same pod, we don't recover monopoly number
    if (instanceSize == 0 && instance.scheduleoption().schedpolicyname() != MONOPOLY_SCHEDULE) {
        info.set_monopolynum(info.monopolynum() + 1);
        total.set_monopolynum(total.monopolynum() + 1);
    }
}

void UpdateBucketInfoAddInstance(const resources::InstanceInfo &instance, const resource_view::Resources &resources,
                                 const int instanceSize, resources::ResourceUnit &view)
{
    auto bucket = GetBucketInfo(resources, view);
    if (bucket == nullptr) {
        YRLOG_WARN("invalid minUnitResource {} while add instance {} to resource view.", resources.ShortDebugString(),
                   instance.instanceid());
        return;
    }
    auto &info = (*bucket->mutable_allocatable())[instance.unitid()];
    auto &total = *bucket->mutable_total();

    if (instanceSize == 1) {
        info.set_monopolynum(info.monopolynum() - 1);
        total.set_monopolynum(total.monopolynum() - 1);
    }
}

resource_view::Resources DeleteInstanceFromAgentView(const resources::InstanceInfo &instance,
                                                     resources::ResourceUnit &unit)
{
    auto nodeLabels = unit.mutable_nodelabels();
    DeleteLabel(instance, *nodeLabels);
    // while monopolized schedule, the allocatable of selected minimum unit(function agent)
    // should be add to equal capacity
    auto addend = instance.resources();
    if (instance.scheduleoption().schedpolicyname() == MONOPOLY_SCHEDULE) {
        addend = unit.capacity();
    }
    (*unit.mutable_allocatable()) = unit.allocatable() + addend;
    // delete instance from bottom level
    (void)unit.mutable_instances()->erase(instance.instanceid());
    return addend;
}
}  // namespace functionsystem