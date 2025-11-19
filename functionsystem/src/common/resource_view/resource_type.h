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

#ifndef COMMON_RESOURCE_VIEW_RESOURCE_TYPE_H
#define COMMON_RESOURCE_VIEW_RESOURCE_TYPE_H

#include <google/protobuf/repeated_field.h>

#include <functional>
#include <unordered_map>

#include "common/proto/pb/posix_pb.h"
#include "common/proto/pb/posix/resource.pb.h"
#include "common/proto/pb/posix/message.pb.h"

namespace functionsystem::resource_view {
const std::string CPU_RESOURCE_NAME = "CPU";
const std::string MEMORY_RESOURCE_NAME = "Memory";
const std::string DISK_RESOURCE_NAME = "disk";
const std::string DEFAULT_NPU_PRODUCT = "310";
const std::string DEFAULT_GPU_PRODUCT = "cuda";
const std::string GPU_RESOURCE_NAME = "GPU";
const std::string NPU_RESOURCE_NAME = "NPU";
const std::string INIT_LABELS_RESOURCE_NAME = "InitLabels";

const uint32_t MULTI_STREAM_DEFAULT_NUM = 100;
const uint32_t HETEROGENEOUS_RESOURCE_REQUIRED_COUNT = 3;
const std::string HETEROGENEOUS_MEM_KEY = "HBM";
const std::string HETEROGENEOUS_LATENCY_KEY = "latency";
const std::string HETEROGENEOUS_STREAM_KEY = "stream";
const std::string HETEROGENEOUS_CARDNUM_KEY = "count";
const std::string HEALTH_KEY = "health";
const std::string IDS_KEY = "ids";
const std::string USED_IDS_KEY = "used_ids";
const std::string DEV_CLUSTER_IPS_KEY = "dev_cluster_ips";
using ProtoRepeatedPtrFiled = ::google::protobuf::RepeatedPtrField<std::string>;

const std::string DISK_MOUNT_POINT = "YR_DISK_MOUNT_POINT";

using Category = ::resources::Value::Vectors::Category;
using Resource = ::resources::Resource;
using Resources = ::resources::Resources;
using DiskContent = ::resources::DiskContent;
using ResourceExtension = ::resources::Resource::Extension;
using InstanceInfo = ::resources::InstanceInfo;
using RouteInfo = ::resources::RouteInfo;
using ResourceUnit = ::resources::ResourceUnit;
using ValueType = ::resources::Value_Type;
using ValueScalar = ::resources::Value_Scalar;
using ValueCounter = ::resources::Value_Counter;
using AffinityType = ::resources::AffinityType;
using BucketIndex = ::resources::BucketIndex;
using Bucket = ::resources::BucketIndex::Bucket;
using BucketInfo = ::resources::BucketIndex::Bucket::Info;
using Addition = ::resources::Addition;
using Deletion = ::resources::Deletion;
using Modification = ::resources::Modification;
using StatusChange = ::resources::StatusChange;
using InstanceChange = ::resources::InstanceChange;
using ResourceUnitChange = ::resources::ResourceUnitChange;
using ResourceUnitChanges = ::resources::ResourceUnitChanges;
using PullResourceRequest = ::messages::PullResourceRequest;

using ResourceUpdateHandler = std::function<void()>;
using ValueToStringFunc = std::function<std::string(const Resource &)>;
using ValueValidateFunc = std::function<bool(const Resource &)>;
using ValueEqualFunc = std::function<bool(const Resource &, const Resource &)>;
using ValueAddFunc = std::function<Resource(const Resource &, const Resource &)>;
using ValueSubFunc = std::function<Resource(const Resource &, const Resource &)>;
using ValueLessFunc = std::function<bool(const Resource &, const Resource &)>;

enum class UpdateType {
    UPDATE_ACTUAL,
    UPDATE_STATIC,
    UPDATE_UNDEFINED,
};

enum class UnitStatus {
    NORMAL = 0,
    EVICTING = 1,
    RECOVERING = 2,
    TO_BE_DELETED = 3,
};

enum class SCHEDULER_LEVEL {
    LOCAL,
    NON_ROOT_DOMAIN,
    ROOT_DOMAIN
};

struct ResourceViewInfo {
    ResourceUnit resourceUnit;
    SCHEDULER_LEVEL schedulerLevel;
    std::unordered_map<std::string, std::string> alreadyScheduled;
    std::unordered_map<std::string, ::google::protobuf::Map<std::string, ValueCounter>> allLocalLabels;
};

struct HeteroDeviceCompare {
    bool operator()(const common::HeteroDeviceInfo &lhs, const common::HeteroDeviceInfo &rhs) const
    {
        return lhs.deviceid() < rhs.deviceid();
    }
};
}  // namespace functionsystem::resource_view

namespace functionsystem::InnerSystemAffinity {
    using ObjAffinity = ::resources::ObjAffinity;
    using TenantAffinity = ::resources::TenantAffinity;
    using PreemptedAffinity = ::resources::PreemptedAffinity;
    using PendingAffinity = ::resources::PendingAffinity;
    using InnerSystemAffinity = ::resources::InnerSystemAffinity;
    using Affinity = ::resources::Affinity;
}

#endif  // COMMON_RESOURCE_VIEW_RESOURCE_TYPE_H
