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

#ifndef LOCAL_SCHEDULER__RESOURCE_GROUP_CTRL_H
#define LOCAL_SCHEDULER__RESOURCE_GROUP_CTRL_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include "common/utils/actor_driver.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/proto/pb/posix_pb.h"

namespace functionsystem::local_scheduler {
class ResourceGroupCtrl : public ActorDriver {
public:
    explicit ResourceGroupCtrl(const std::shared_ptr<BasisActor> &actor) : ActorDriver(actor), actor_(actor) {}
    ~ResourceGroupCtrl() override = default;

    static std::shared_ptr<ResourceGroupCtrl> Init();

    virtual litebus::Future<std::shared_ptr<CreateResourceGroupResponse>> Create(
        const std::string &from, const std::shared_ptr<CreateResourceGroupRequest> &req);

    virtual litebus::Future<KillResponse> Kill(const std::string &from, const std::string &srcTenantID,
                                               const std::shared_ptr<KillRequest> &killReq);

private:
    std::shared_ptr<BasisActor> actor_;
};
}  // namespace functionsystem::local_scheduler
#endif  // LOCAL_SCHEDULER__RESOURCE_GROUP_CTRL_H
