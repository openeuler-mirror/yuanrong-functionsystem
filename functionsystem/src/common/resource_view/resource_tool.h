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

#ifndef COMMON_RESOURCE_VIEW_RESOURCE_TOOL_H
#define COMMON_RESOURCE_VIEW_RESOURCE_TOOL_H

#include <numeric>
#include <utils/string_utils.hpp>

#include "google/protobuf/util/json_util.h"

#include "async/option.hpp"
#include "async/uuid_generator.hpp"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "resource_type.h"

namespace functionsystem::resource_view {
extern const std::unordered_map<ValueType, ValueToStringFunc> GLOBAL_VALUE_TO_STRING_FUNCS;
extern const std::unordered_map<ValueType, ValueValidateFunc> GLOBAL_VALUE_VALIDATE_FUNCS;
extern const std::unordered_map<ValueType, ValueValidateFunc> GLOBAL_VALUE_IS_EMPTY_FUNCS;
extern const std::unordered_map<ValueType, ValueEqualFunc> GLOBAL_VALUE_IS_EQUAL_FUNCS;
extern const std::unordered_map<ValueType, ValueAddFunc> GLOBAL_VALUE_ADD_FUNCS;
extern const std::unordered_map<ValueType, ValueSubFunc> GLOBAL_VALUE_SUB_FUNCS;
extern const std::unordered_map<ValueType, ValueLessFunc> GLOBAL_VALUE_LESS_FUNCS;

inline ResourceUnit InitResource(const std::string &id)
{
    ResourceUnit unit;
    resource_view::Resource r;
    r.set_type(ValueType::Value_Type_SCALAR);
    r.mutable_scalar()->set_value(0.0);
    r.mutable_scalar()->set_limit(0.0);
    resource_view::Resources rs;
    r.set_name(CPU_RESOURCE_NAME);
    (*rs.mutable_resources())[CPU_RESOURCE_NAME] = r;
    r.set_name(MEMORY_RESOURCE_NAME);
    (*rs.mutable_resources())[MEMORY_RESOURCE_NAME] = r;
    (*unit.mutable_capacity()) = rs;
    (*unit.mutable_allocatable()) = rs;
    (*unit.mutable_actualuse()) = rs;
    unit.set_id(id);
    unit.set_revision(0);
    litebus::uuid_generator::UUID uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    unit.set_viewinittime(uuid.ToString());
    unit.set_ownerid(id);
    return unit;
}

template <typename T>
inline std::string Join(const T &str, const std::string delimiter)
{
    return std::accumulate(
        str.begin(), str.end(), std::string(),
        [&delimiter](const std::string &x, const std::string &y) { return x.empty() ? y : x + delimiter + y; });
}

template <typename T>
inline std::string CommaSepStr(const T &str)
{
    return Join<T>(str, ",");
}

inline bool IsValidType(const Resource &resource)
{
    bool r = false;
    for (auto t = ValueType::Value_Type_SCALAR; t < ValueType::Value_Type_END; t = static_cast<ValueType>(t + 1)) {
        if (resource.type() == t) {
            r = true;
            break;
        }
    }
    return r;
}

inline bool IsValid(const Resource &resource)
{
    if (resource.name().empty() || !IsValidType(resource)) {
        YRLOG_WARN("invalid resource : empty resource name or invalid type.");
        return false;
    }

    auto it = GLOBAL_VALUE_VALIDATE_FUNCS.find(resource.type());
    ASSERT_FS(it != GLOBAL_VALUE_VALIDATE_FUNCS.end());
    return it->second(resource);
}

inline bool IsEmpty(const Resource &resource)
{
    ASSERT_FS(IsValid(resource));
    auto it = GLOBAL_VALUE_IS_EMPTY_FUNCS.find(resource.type());
    ASSERT_FS(it != GLOBAL_VALUE_IS_EMPTY_FUNCS.end());

    return it->second(resource);
}

inline bool IsValid(const Resources &resources)
{
    if (resources.resources().empty()) {
        YRLOG_WARN("resources is invalid because size is 0.");
        return false;
    }

    bool ret = true;
    for (auto &r : resources.resources()) {
        if (!IsValid(r.second)) {
            ret = false;
            break;
        }
    }
    return ret;
}

inline bool IsEmpty(const Resources &resources)
{
    ASSERT_FS(IsValid(resources));
    if (resources.resources().size() == 0) {
        return true;
    }

    bool ret = true;
    for (auto &r : resources.resources()) {
        if (!IsEmpty(r.second)) {
            ret = false;
            break;
        }
    }
    return ret;
}

inline bool IsRequestSatisfiable(double req, double available)
{
    if (req < available || abs(req - available) <= EPSINON) {
        return true;
    }
    return false;
}

inline bool IsVectorsAvailable(const Category &available, const resources::Resource &req)
{
    auto reqVal = req.scalar().value();
    for (const auto &pair : available.vectors()) {
        for (const auto &availableVal : pair.second.values()) {
            if (IsRequestSatisfiable(reqVal, availableVal)) {
                return true;
            }
        }
    }
    return false;
}

inline std::string ToString(const Resource &resource)
{
    ASSERT_FS(IsValid(resource));

    auto f = GLOBAL_VALUE_TO_STRING_FUNCS.find(resource.type());
    if (f == GLOBAL_VALUE_TO_STRING_FUNCS.end()) {
        return "Unknown";
    }

    return f->second(resource);
}

inline std::string ToString(const Resources &resources)
{
    std::string s;
    for (const auto &r : resources.resources()) {
        auto str = ToString(r.second);
        s += str + " ";
    }

    auto ret = "{ " + s + "}";
    return ret;
}

inline std::string ToString(const ResourceUnit &unit)
{
    std::string str;
    auto ret = google::protobuf::util::MessageToJsonString(unit, &str);
    if (!ret.ok()) {
        YRLOG_ERROR("resource unit to string failed, error info is {}.", ret.ToString());
    }
    return str;
}

inline bool HasValidCPU(const Resources &resources)
{
    if (resources.resources().find(CPU_RESOURCE_NAME) == resources.resources().end()) {
        return false;
    }
    return true;
}

inline bool HasValidMemory(const Resources &resources)
{
    if (resources.resources().find(MEMORY_RESOURCE_NAME) == resources.resources().end()) {
        return false;
    }
    return true;
}

inline void GenerateMinimumUnitBucketInfo(ResourceUnit &unit)
{
    if (unit.id().empty() || !unit.has_capacity() || !unit.has_allocatable() || !IsValid(unit.capacity()) ||
        !IsValid(unit.allocatable())) {
        return;
    }
    if (!HasValidCPU(unit.allocatable()) || !HasValidMemory(unit.allocatable())) {
        return;
    }
    auto cpu = unit.allocatable().resources().at(CPU_RESOURCE_NAME).scalar().value();
    auto mem = unit.allocatable().resources().at(MEMORY_RESOURCE_NAME).scalar().value();
    if (abs(cpu) < EPSINON) {
        return;
    }
    auto proportion = mem / cpu;
    auto &bucketIndex = (*unit.mutable_bucketindexs())[std::to_string(proportion)];
    auto &bucket = (*bucketIndex.mutable_buckets())[std::to_string(mem)];
    auto &info = (*bucket.mutable_allocatable())[unit.id()];
    bucket.mutable_total()->set_monopolynum(1);
    bucket.mutable_total()->set_sharednum(0);
    info.set_monopolynum(1);
    info.set_sharednum(0);
}

inline bool HasInstanceAffinity(const resource_view::InstanceInfo &instance)
{
    const auto &instanceAffinity = instance.scheduleoption().affinity().instance();
    return !instanceAffinity.requiredaffinity().condition().subconditions().empty()
           || !instanceAffinity.requiredantiaffinity().condition().subconditions().empty()
           || !instanceAffinity.preferredaffinity().condition().subconditions().empty()
           || !instanceAffinity.preferredantiaffinity().condition().subconditions().empty();
}

inline bool HasResourceAffinity(const resource_view::InstanceInfo &instance)
{
    const auto &resourceAffinity = instance.scheduleoption().affinity().resource();
    return !resourceAffinity.requiredaffinity().condition().subconditions().empty()
           || !resourceAffinity.requiredantiaffinity().condition().subconditions().empty()
           || !resourceAffinity.preferredaffinity().condition().subconditions().empty()
           || !resourceAffinity.preferredantiaffinity().condition().subconditions().empty();
}

inline bool HasInnerAffinity(const resource_view::InstanceInfo &instance)
{
    const auto &innerAffinity = instance.scheduleoption().affinity().inner();
    return !innerAffinity.data().preferredaffinity().condition().subconditions().empty()
           || !innerAffinity.preempt().preferredaffinity().condition().subconditions().empty()
           || !innerAffinity.preempt().preferredantiaffinity().condition().subconditions().empty()
           || !innerAffinity.tenant().preferredaffinity().condition().subconditions().empty()
           || !innerAffinity.tenant().requiredantiaffinity().condition().subconditions().empty();
}

inline bool HasAffinity(const resource_view::InstanceInfo &instance)
{
    return !instance.scheduleoption().affinity().instanceaffinity().affinity().empty() || HasInstanceAffinity(instance)
           || HasResourceAffinity(instance) || HasInnerAffinity(instance);
}

inline bool IsHeterogeneousResource(const std::string &resourceName)
{
    auto resourceNameFields = litebus::strings::Split(resourceName, "/");
    if (resourceNameFields.size() == HETERO_RESOURCE_FIELD_NUM) {
        return true;
    }
    return false;
}

inline bool HasHeterogeneousResource(const resource_view::InstanceInfo &instance)
{
    for (auto &req : instance.resources().resources()) {
        if (IsHeterogeneousResource(req.first)) {
            return true;
        }
    }
    return false;
}

inline bool IsDiskResource(const std::string &resourceName)
{
    return resourceName == resource_view::DISK_RESOURCE_NAME;
}

inline bool HasDiskResource(const resource_view::InstanceInfo &instance)
{
    if (instance.resources().resources().find(DISK_RESOURCE_NAME) == instance.resources().resources().end()) {
        return false;
    }
    return true;
}

inline bool HasDiskResource(const ResourceUnit &unit)
{
    if (unit.allocatable().resources().count(resource_view::DISK_RESOURCE_NAME) == 0) {
        return false;
    }
    if (unit.allocatable().resources().at(resource_view::DISK_RESOURCE_NAME).vectors().values()
        .count(resource_view::DISK_RESOURCE_NAME) == 0) {
        return false;
    }
    return true;
}

}  // namespace functionsystem::resource_view

namespace functionsystem {

bool operator<=(const resource_view::Resource &l, const resource_view::Resource &r);
bool operator==(const resource_view::Resource &l, const resource_view::Resource &r);
bool operator!=(const resource_view::Resource &l, const resource_view::Resource &r);
resource_view::Resource operator+(const resource_view::Resource &l, const resource_view::Resource &r);
resource_view::Resource operator-(const resource_view::Resource &l, const resource_view::Resource &r);

bool operator<=(const resource_view::Resources &l, const resource_view::Resources &r);
bool operator>(const resource_view::Resources &l, const resource_view::Resources &r);
bool operator==(const resource_view::Resources &l, const resource_view::Resources &r);
bool operator!=(const resource_view::Resources &l, const resource_view::Resources &r);
resource_view::Resources operator+(const resource_view::Resources &left, const resource_view::Resources &right);
resource_view::Resources operator-(const resource_view::Resources &left, const resource_view::Resources &right);

resources::Value::Counter operator+(const resources::Value::Counter &l, const resources::Value::Counter &r);
resources::Value::Counter operator-(const resources::Value::Counter &l, const resources::Value::Counter &r);

using MapCounter = ::google::protobuf::Map<std::string, ::resources::Value::Counter>;
MapCounter operator+(const MapCounter &l, const MapCounter &r);
MapCounter operator-(const MapCounter &l, const MapCounter &r);
MapCounter ToLabelKV(const std::string &label);
MapCounter ToLabelKVs(const ::google::protobuf::RepeatedPtrField<std::string> &labels);
MapCounter ToLabelKVs(const ::google::protobuf::Map<std::string, std::string> &labels);
resource_view::Resources BuildResources(int64_t cpuVal, int64_t memVal);
void DeleteLabel(const resources::InstanceInfo &instInfo,
                 ::google::protobuf::Map<std::string, resource_view::ValueCounter> &nodeLabels);
resource_view::Bucket *GetBucketInfo(const resource_view::Resources &resources, resources::ResourceUnit &view);
void UpdateBucketInfoDelInstance(const resources::InstanceInfo &instance, const resource_view::Resources &resources,
                                 const int instanceSize, resources::ResourceUnit &view);
void UpdateBucketInfoAddInstance(const resources::InstanceInfo &instance, const resource_view::Resources &resources,
                                 const int instanceSize, resources::ResourceUnit &view);
resource_view::Resources DeleteInstanceFromAgentView(const resources::InstanceInfo &instance,
                                                     resources::ResourceUnit &unit);
template <typename T>
std::string DebugProtoMapString(const ::google::protobuf::Map<std::string, T> &map)
{
    std::string s;
    for (const auto &item : map) {
        s += "[" + item.first + ":" + item.second.ShortDebugString() + "] ";
    }
    return s;
}
}  // namespace functionsystem

#endif  // COMMON_RESOURCE_VIEW_RESOURCE_TOOL_H
