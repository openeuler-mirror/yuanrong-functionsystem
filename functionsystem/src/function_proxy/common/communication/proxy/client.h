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
#ifndef PROXY_CLIENT_H
#define PROXY_CLIENT_H
#include "actor.h"
#include "function_proxy/common/communication/rpc_client/forward_rpc.h"

namespace functionsystem::proxy {
class Client : public ForwardRPC {
public:
    explicit Client(const litebus::AID &dst) : dst_(dst)
    {
    }
    ~Client() override = default;
    static void SetProxy(const std::shared_ptr<Actor> &actor)
    {
        actor_ = actor;
    }

    // the instance was not deployed in this node after instance was migrated/created, the Dst AID should be updated.
    void UpdateDstAID(const litebus::AID &dst);
    litebus::Future<internal::ForwardCallResponse> Call(
        const std::shared_ptr<internal::ForwardCallRequest> &request) override;
    litebus::Future<internal::ForwardCallResultResponse> CallResult(
        const internal::ForwardCallResultRequest &request) override;

    std::string GetClientInfo() const
    {
        std::string ret;
        ret += "[dst: " + dst_.HashString() + ", aid: ";
        if (actor_ != nullptr) {
            ret += actor_->GetAID().HashString();
        }
        ret += "]";
        return ret;
    }

    std::string GetDstAddress() const
    {
        return dst_.Url();
    }

private:
    litebus::AID dst_;
    inline static std::shared_ptr<Actor> actor_;
};
}  // namespace functionsystem::proxy
#endif
