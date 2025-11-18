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
#ifndef FUNCTIONSYSTEM_MONITOR_CALLBACK_ACTOR_H
#define FUNCTIONSYSTEM_MONITOR_CALLBACK_ACTOR_H

#include <string>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem {

struct Monitor {
    std::string topDirectoryPath;
    int64_t totalSize = 0;  // unit: B
};

class MonitorCallBackActor : public litebus::ActorBase {
public:
    MonitorCallBackActor(const std::string &name,
                         const litebus::AID &functionAgentAID);
    ~MonitorCallBackActor() override = default;

    litebus::Future<Status> AddToMonitorMap(const std::string &instanceID, const std::string &workPath,
                                            const std::shared_ptr<messages::StartInstanceRequest> &request);

    litebus::Future<std::string> DeleteFromMonitorMap(const std::string &instanceID);

    void DeleteAllMonitorAndRemoveDir();

    void CheckIfExceedQuotaCallBack(const std::string &insID,
                                    const std::shared_ptr<messages::StartInstanceRequest> &request);

    litebus::Future<Status> SendMessage(const std::string &requestID, const std::string &instanceID,
                                        const int64_t quota, const std::string &topPath);

protected:
    void Init() override;
    void Finalize() override;

private:
    litebus::Future<int64_t> GetDiskUsage(const std::string &path) const;
    bool IsDiskUsageOverLimit(const litebus::Future<int64_t> &usage,
                        const std::shared_ptr<messages::StartInstanceRequest> &request);
    std::unordered_map<std::string, std::shared_ptr<Monitor>> allMonitors_;
    litebus::AID functionAgentAID_;
};
}  // namespace functionsystem
#endif  // FUNCTIONSYSTEM_MONITOR_CALLBACK_ACTOR_H