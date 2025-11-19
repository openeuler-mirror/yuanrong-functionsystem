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

#ifndef FUNCTIONCORE_CPP_INSTANCE_MANAGER_UTIL_H
#define FUNCTIONCORE_CPP_INSTANCE_MANAGER_UTIL_H

#include "common/types/instance_state.h"
#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem {
[[maybe_unused]] static bool GenInstanceInfoJson(const std::shared_ptr<resources::InstanceInfo> &instanceInfo,
                                                 const InstanceState &state, const std::string &msg,
                                                 const int64_t version, std::string &output)
{
    if (instanceInfo == nullptr) {
        return false;
    }
    instanceInfo->mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    instanceInfo->mutable_instancestatus()->set_msg(msg);
    instanceInfo->mutable_instancestatus()->set_exitcode(static_cast<int32_t>(StatusCode::ERR_INSTANCE_EXITED));
    instanceInfo->set_functionproxyid(INSTANCE_MANAGER_OWNER);
    instanceInfo->set_version(version);
    return TransToJsonFromInstanceInfo(output, *instanceInfo);
}

[[maybe_unused]] bool GeneratePutInfo(const std::shared_ptr<resource_view::InstanceInfo> &instance,
                                      const std::shared_ptr<StoreInfo> &instancePutInfo,
                                      const std::shared_ptr<StoreInfo> &routePutInfo, const InstanceState &transState,
                                      const std::string &msg)
{
    if (instance == nullptr) {
        return false;
    }

    auto version = instance->version();
    if (version > INT64_MAX - 1) {
        YRLOG_ERROR("{}|version({}) add operation will exceed the maximum value({}) of INT64", instance->requestid(),
                    version, INT64_MAX);
        return false;
    }

    auto instanceKey = GenInstanceKey(instance->function(), instance->instanceid(), instance->requestid());
    auto routeKey = GenInstanceRouteKey(instance->instanceid());
    if (instanceKey.IsNone()) {
        YRLOG_ERROR("{}|{} failed to generate key", instance->requestid(), instance->instanceid());
        return false;
    }

    std::string instanceJsonStr;
    if (!GenInstanceInfoJson(instance, transState, msg, version + 1, instanceJsonStr)) {
        YRLOG_ERROR("failed to transfer InstanceInfo to json for key: {}", instanceKey.Get());
        return false;
    }
    resource_view::RouteInfo routeInfo;
    std::string routeJsonStr;
    TransToRouteInfoFromInstanceInfo(*instance, routeInfo);
    if (!TransToJsonFromRouteInfo(routeJsonStr, routeInfo)) {
        YRLOG_ERROR("failed to transfer RouteInfo to json for key: {}", routeKey);
        return false;
    }

    routePutInfo->key = routeKey;
    routePutInfo->value = routeJsonStr;
    instancePutInfo->key = instanceKey.Get();
    instancePutInfo->value = instanceJsonStr;
    return true;
}
}
#endif  // FUNCTIONCORE_CPP_INSTANCE_MANAGER_UTIL_H
