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
#include "common/file_monitor/monitor_callback_actor.h"

#include "common/constants/constants.h"

namespace functionsystem {
namespace {
const int32_t RECYCLE_DURATION = 5000;  // MS
const int64_t MEGA_BYTES = 1024;
};

MonitorCallBackActor::MonitorCallBackActor(const std::string &name,
                                           const litebus::AID &functionAgentAID)
    : ActorBase(name), functionAgentAID_(functionAgentAID)
{
}

void MonitorCallBackActor::Init()
{
}

void MonitorCallBackActor::Finalize()
{
}

litebus::Future<Status> MonitorCallBackActor::AddToMonitorMap(
    const std::string &instanceID, const std::string &workPath,
    const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    if (allMonitors_.count(instanceID) != 0) {
        return Status::OK();
    }
    allMonitors_[instanceID] = std::make_shared<Monitor>();
    // add INSTANCE_WORK_DIR env
    allMonitors_[instanceID]->topDirectoryPath = workPath;
    if (request->runtimeinstanceinfo().runtimeconfig().subdirectoryconfig().quota() > 0) {
        litebus::AsyncAfter(RECYCLE_DURATION, GetAID(), &MonitorCallBackActor::CheckIfExceedQuotaCallBack, instanceID,
                            request);
    }
    return Status::OK();
}

litebus::Future<std::string> MonitorCallBackActor::DeleteFromMonitorMap(const std::string &instanceID)
{
    // clear work dir if exist
    std::string path = "";
    if (const auto iter = allMonitors_.find(instanceID);
        !instanceID.empty() && iter != allMonitors_.end() && iter->second != nullptr) {
        path = allMonitors_[instanceID]->topDirectoryPath;
        allMonitors_.erase(instanceID);
    }
    if (!path.empty()) {
        (void)litebus::os::Rmdir(path);
    }
    return path;
}

void MonitorCallBackActor::DeleteAllMonitorAndRemoveDir()
{
    if (allMonitors_.empty()) {
        return;
    }
    for (const auto &monitors : allMonitors_) {
        (void)litebus::os::Rmdir(monitors.second->topDirectoryPath);
    }
    allMonitors_.clear();
}

void MonitorCallBackActor::CheckIfExceedQuotaCallBack(const std::string &insID,
                                                      const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    if (allMonitors_.count(insID) == 0) {
        return;
    }

    auto &monitor = allMonitors_[insID];
    auto topPath = monitor->topDirectoryPath;
    auto usage = GetDiskUsage(topPath);
    if (IsDiskUsageOverLimit(usage, request)) {
        return;
    }
    litebus::AsyncAfter(RECYCLE_DURATION, GetAID(), &MonitorCallBackActor::CheckIfExceedQuotaCallBack, insID, request);
}

litebus::Future<int64_t> MonitorCallBackActor::GetDiskUsage(const std::string &path) const
{
    if (!FileExists(path)) {
        YRLOG_DEBUG("watch of path: {} already deleted");
        return -1;
    }

    if (!CheckIllegalChars(path)) {  // Blacklist checks for illegal characters.
        YRLOG_ERROR("path contains illegal chars");
        return -1;
    }

    std::string command = "/usr/bin/du -sh -k " + path;
    auto result = ExecuteCommandByPopen(command, INT32_MAX);
    if (result.empty()) {
        YRLOG_ERROR("get disk({}) usage failed. error message", path);
        return -1;
    }
    auto outStrs = litebus::strings::Split(result, "\t");
    if (outStrs.empty()) {
        YRLOG_ERROR("failed to get disk({}) usage, empty output", path);
        return -1;
    }

    int64_t usage = 0;
    try {
        usage = std::stoi(outStrs[0]);
    } catch (std::invalid_argument const &ex) {
        YRLOG_ERROR("failed to get disk({}) usage,  value({}) is not INT", path, outStrs[0]);
        return -1;
    }
    return usage;
}

litebus::Future<Status> MonitorCallBackActor::SendMessage(const std::string &requestID, const std::string &instanceID,
                                                          const int64_t quota, const std::string &topPath)
{
    auto req = std::make_shared<messages::UpdateInstanceStatusRequest>();
    req->set_requestid(requestID);

    auto info = req->mutable_instancestatusinfo();
    info->set_instanceid(instanceID);
    info->set_status(static_cast<int32_t>(INSTANCE_DISK_USAGE_EXCEED_LIMIT));
    info->set_type(static_cast<int32_t>(EXIT_TYPE::EXCEPTION_INFO));
    info->set_requestid(requestID);
    info->set_instancemsg("disk usage exceed limit: " + std::to_string(quota) + "MB");
    YRLOG_INFO("{}|instance({}) path: {} exceed limit: {}MB", req->requestid(), instanceID, topPath, quota);
    (void)Send(functionAgentAID_, "UpdateInstanceStatus", req->SerializeAsString());
    return Status::OK();
}

bool MonitorCallBackActor::IsDiskUsageOverLimit(const litebus::Future<int64_t> &usage,
                                                const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    auto insID = request->runtimeinstanceinfo().instanceid();
    if (allMonitors_.count(insID) == 0) {
        YRLOG_INFO("instance({}) is not exited, stop monitor.", insID);
        return true;
    }

    auto &monitor = allMonitors_[insID];
    auto topPath = monitor->topDirectoryPath;

    if (usage.IsError() || usage.Get() < 0) {
        YRLOG_WARN("{}|cannot get path: {} usage", request->runtimeinstanceinfo().requestid(), topPath);
        return false;
    }
    monitor->totalSize = usage.Get();

    int64_t quota = request->runtimeinstanceinfo().runtimeconfig().subdirectoryconfig().quota();
    int64_t totalSizeInMB = monitor->totalSize / MEGA_BYTES;
    if (totalSizeInMB <= quota) {
        return false;
    }

    auto runtimeID = request->runtimeinstanceinfo().runtimeid();
    auto instanceID = request->runtimeinstanceinfo().instanceid();
    auto requestID = litebus::os::Join("update-instance-status-request", runtimeID, '-');
    SendMessage(requestID, instanceID, quota, topPath);
    return true;
}
}  // namespace functionsystem