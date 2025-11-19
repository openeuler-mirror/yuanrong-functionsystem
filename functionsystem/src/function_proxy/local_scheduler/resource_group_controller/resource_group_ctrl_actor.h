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

#ifndef LOCAL_SCHEDULER_RESOURCE_GROUP_CTRL_ACTOR_H
#define LOCAL_SCHEDULER_RESOURCE_GROUP_CTRL_ACTOR_H

#include "common/utils/actor_driver.h"
#include "common/proto/pb/posix_pb.h"
#include "common/explorer/explorer.h"
#include "common/utils/request_sync_helper.h"

namespace functionsystem::local_scheduler {
class ResourceGroupCtrlActor : public BasisActor {
public:
    ResourceGroupCtrlActor() : BasisActor("ResourceGroupCtrlActor")
    {
        rgMgrAid_ = std::make_shared<litebus::AID>();
    }
    ~ResourceGroupCtrlActor() = default;

    litebus::Future<std::shared_ptr<CreateResourceGroupResponse>> Create(
        const std::string &from, const std::shared_ptr<CreateResourceGroupRequest> &req);
    litebus::Future<KillResponse> Kill(const std::string &from, const std::string &srcTenantID,
                                       const std::shared_ptr<KillRequest> &killReq);
    void OnForwardCreateResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg);
    void OnForwardDeleteResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg);

protected:
    void Init() override;

private:
    void UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo);
private:
    std::shared_ptr<litebus::AID> rgMgrAid_;
    BACK_OFF_RETRY_HELPER(ResourceGroupCtrlActor, std::shared_ptr<CreateResourceGroupResponse>, createHelper_);
    BACK_OFF_RETRY_HELPER(ResourceGroupCtrlActor, inner_service::ForwardKillResponse, killHelper_);
};
}  // namespace functionsystem::local_scheduler
#endif  // LOCAL_SCHEDULER_RESOURCE_GROUP_CTRL_ACTOR_H
