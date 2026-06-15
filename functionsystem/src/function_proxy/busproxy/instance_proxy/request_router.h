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

#ifndef KERNEL_PROJECT_REQUEST_ROUTER_H
#define KERNEL_PROJECT_REQUEST_ROUTER_H

#include "actor/actor.hpp"
#include "common/constants/actor_name.h"
#include "common/proto/pb/posix_pb.h"
#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"

namespace functionsystem::busproxy {
class RequestRouter : public litebus::ActorBase {
public:
    explicit RequestRouter(const std::string &name) : litebus::ActorBase(name)
    {
    }
    ~RequestRouter() override = default;

    void ForwardCall(const litebus::AID &from, std::string &&, std::string &&msg);

    static void BindObserver(const std::shared_ptr<function_proxy::DataPlaneObserver> &observer)
    {
        observer_ = observer;
    }

protected:
    void Init() override;

private:
    void OnMissingActorRouteQuery(const litebus::AID &from, const internal::RouteCallRequest &routeReq,
                                  const litebus::Future<function_proxy::DirectRouteQueryResult> &routeFuture);

    inline static std::shared_ptr<function_proxy::DataPlaneObserver> observer_ { nullptr };
};
}  // namespace functionsystem::busproxy

#endif  // KERNEL_PROJECT_REQUEST_ROUTER_H
