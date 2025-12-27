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

#include "data_obj_actor.h"

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "utils/string_utils.hpp"

namespace functionsystem {
const uint32_t IP_PORT_LENGTH = 2;

bool ValueComp(const std::pair<std::string, uint64_t> &a, const std::pair<std::string, uint64_t> &b)
{
    return a.second > b.second;
}

std::vector<std::string> DataObjActor::GetObjNodeList(const std::string &tenantId,
                                                      const std::vector<std::string> &objIDs)
{
    std::vector<ObjMetaInfo> objMetaInfo;
    if (distributedCacheClient_ == nullptr ||
        distributedCacheClient_->GetObjMetaInfo(tenantId, objIDs, objMetaInfo).IsError()) {
        YRLOG_ERROR("get object metadata failed");
        return {};
    }

    std::unordered_map<std::string, uint64_t> nodeObjSizeMap;
    for (const auto &meta : objMetaInfo) {
        for (const auto &local : meta.locations) {
            nodeObjSizeMap[local] += meta.objSize;  // add size by node
        }
    }

    // sort by size desc
    std::vector<std::pair<std::string, uint64_t>> vec(nodeObjSizeMap.begin(), nodeObjSizeMap.end());
    std::sort(vec.begin(), vec.end(), ValueComp);
    std::vector<std::string> workerIDList;
    for (const auto &pair : vec) {
        workerIDList.push_back(pair.first);
    }

    std::vector<std::string> nodeList;
    if (distributedCacheClient_->GetWorkerAddrByWorkerId(workerIDList, nodeList).IsError()) {
        YRLOG_ERROR("get object location failed");
        return {};
    }

    std::vector<std::string> result;
    for (const auto &node : nodeList) {
        auto strs = litebus::strings::Split(node, ":");  // ip:port
        if (strs.size() != IP_PORT_LENGTH) {
            YRLOG_WARN("get object location failed, invalid location({})", node);
            continue;
        }
        result.push_back(strs[0]);
    }
    return result;
}

void DataObjActor::InitDistributedCacheClient()
{
    RETURN_IF_NULL(distributedCacheClient_);
    if (auto status = distributedCacheClient_->Init(); status.IsOk()) {
        YRLOG_INFO("succeed to init data obj client");
        return;
    }
    YRLOG_WARN("failed to init data obj client, try to reconnect");
    const uint32_t stateClientInitRetryPeriod = 1000;  // ms
    litebus::AsyncAfter(stateClientInitRetryPeriod, GetAID(), &DataObjActor::InitDistributedCacheClient);
}

}  // namespace functionsystem