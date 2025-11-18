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

#ifndef FUNCTION_MASTER_SYSTEM_FUNCTION_POD_MANAGER_H
#define FUNCTION_MASTER_SYSTEM_FUNCTION_POD_MANAGER_H

#include <string>
#include <unordered_set>

#include "common/kube_client/model/pod/v1_pod.h"
#include "common/status/status.h"

namespace functionsystem::scaler {

using V1Pod = functionsystem::kube_client::model::V1Pod;

class SystemFunctionPodManager {
public:
    SystemFunctionPodManager(const std::string &systemFuncName,
                             std::unordered_map<std::string, std::string> &labelSelector)
        : systemFuncName_(systemFuncName), labelSelector_(std::move(labelSelector))
    {
        auto splits = litebus::strings::Split(systemFuncName_, "-");
        systemFuncLastName_ = splits[splits.size() - 1];
    }

    ~SystemFunctionPodManager() = default;

    void OnPodUpdate(const std::shared_ptr<V1Pod> &pod);
    void OnPodDelete(const std::shared_ptr<V1Pod> &pod);
    Status CheckSystemFunctionNeedScale();

    void RegisterScaleUpHandler(const std::function<void(const std::string &, uint32_t)> &cb)
    {
        scaleUpHandler_ = cb;
    }

    void RegisterScaleDownHandler(const std::function<void(const std::string &)> &cb)
    {
        scaleDownHandler_ = cb;
    }

private:
    bool IsPodMatch(const std::shared_ptr<V1Pod> &pod);

    std::string systemFuncName_;
    std::unordered_map<std::string, std::string> labelSelector_;
    std::string systemFuncLastName_;
    uint32_t latestInstanceNum_{ 0 };
    bool isFirst_ {true};
    std::unordered_map<std::string, std::unordered_set<std::string>> nodeID2PodNames_;
    std::unordered_map<std::string, std::unordered_set<std::string>> nodeID2SystemFuncPodNames_;
    std::function<void(const std::string &)> scaleDownHandler_ {nullptr};
    std::function<void(const std::string &, uint32_t)> scaleUpHandler_{ nullptr };
};
}  // namespace functionsystem::scaler

#endif  // FUNCTION_MASTER_SYSTEM_FUNCTION_POD_MANAGER_H
