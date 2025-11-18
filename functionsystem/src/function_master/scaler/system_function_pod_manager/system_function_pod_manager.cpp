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

#include "system_function_pod_manager.h"

#include "function_master/scaler/utils/parse_helper.h"

namespace functionsystem::scaler {

bool SystemFunctionPodManager::IsPodMatch(const std::shared_ptr<V1Pod> &pod)
{
    if (pod == nullptr || pod->GetMetadata() == nullptr || pod->GetMetadata()->GetLabels().empty()) {
        return false;
    }
    auto &labels = pod->GetMetadata()->GetLabels();
    for (const auto &iter : labelSelector_) {
        if (auto it = labels.find(iter.first); it == labels.end() || it->second != iter.second) {
            return false;
        }
    }
    return true;
}

void SystemFunctionPodManager::OnPodUpdate(const std::shared_ptr<V1Pod> &pod)
{
    if (pod->GetStatus() != nullptr && pod->GetStatus()->GetPhase() == "Failed") {
        OnPodDelete(pod);
        return;
    }
    if (!IsPodReady(pod)) {
        return;
    }
    if (IsPodTerminating(pod)) {
        OnPodDelete(pod);
        return;
    }
    auto podName = pod->GetMetadata()->GetName();
    auto nodeName = pod->GetSpec()->GetNodeName();
    if (nodeName.empty()) {
        return;
    }
    if (podName.find(systemFuncLastName_) != std::string::npos) {
        // system func pod
        if (nodeID2SystemFuncPodNames_.find(nodeName) == nodeID2SystemFuncPodNames_.end()) {
            nodeID2SystemFuncPodNames_[nodeName] = {};
        }
        nodeID2SystemFuncPodNames_[nodeName].insert(podName);
    } else {
        if (!IsPodMatch(pod)) {
            return;
        }
        if (nodeID2PodNames_.find(nodeName) == nodeID2PodNames_.end()) {
            nodeID2PodNames_[nodeName] = {};
        }
        nodeID2PodNames_[nodeName].insert(podName);
    }
}

void SystemFunctionPodManager::OnPodDelete(const std::shared_ptr<V1Pod> &pod)
{
    auto nodeName = pod->GetSpec()->GetNodeName();
    if (nodeName.empty()) {
        return;
    }
    auto podName = pod->GetMetadata()->GetName();
    if (podName.find(systemFuncLastName_) != std::string::npos) {
        if (nodeID2SystemFuncPodNames_.find(nodeName) == nodeID2SystemFuncPodNames_.end()) {
            return;
        }
        (void)nodeID2SystemFuncPodNames_[nodeName].erase(podName);
        if (nodeID2SystemFuncPodNames_[nodeName].empty()) {
            nodeID2SystemFuncPodNames_.erase(nodeName);
        }
    } else {
        if (nodeID2PodNames_.find(nodeName) == nodeID2PodNames_.end()) {
            return;
        }
        (void)nodeID2PodNames_[nodeName].erase(podName);
        if (nodeID2PodNames_[nodeName].empty()) {
            nodeID2PodNames_.erase(nodeName);
        }
    }
}

Status SystemFunctionPodManager::CheckSystemFunctionNeedScale()
{
    if (scaleUpHandler_ == nullptr || scaleDownHandler_ == nullptr) {
        return Status::OK();
    }
    for (const auto &it : nodeID2SystemFuncPodNames_) {
        if (nodeID2PodNames_.find(it.first) != nodeID2PodNames_.end()) {
            continue;
        }
        // pod need to be deleted
        for (const auto &podName : it.second) {
            scaleDownHandler_(podName);
        }
    }
    auto cnt = nodeID2PodNames_.size();
    if (scaleUpHandler_ && (isFirst_ || cnt != latestInstanceNum_)) {
        YRLOG_INFO("start to trigger scale up {} from {} to {}", systemFuncName_, latestInstanceNum_, cnt);
        scaleUpHandler_(systemFuncName_, cnt);
        latestInstanceNum_ = cnt;
        isFirst_ = false;
    }
    return Status::OK();
}
}  // namespace functionsystem::scaler