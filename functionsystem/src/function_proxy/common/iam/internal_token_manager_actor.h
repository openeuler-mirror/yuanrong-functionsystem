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

#ifndef FUNCTION_PROXY_COMMON_IAM_INTERNAL_TOKEN_MANAGER_ACTOR_H
#define FUNCTION_PROXY_COMMON_IAM_INTERNAL_TOKEN_MANAGER_ACTOR_H

#include "actor/actor.hpp"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "iam_client.h"

namespace functionsystem::function_proxy {
class InternalTokenManagerActor : public litebus::ActorBase {
public:
    InternalTokenManagerActor(const std::string &name, const std::string &clusterID)
        : litebus::ActorBase(name), clusterID_(clusterID){};

    ~InternalTokenManagerActor() override = default;

    Status Register();

    litebus::Future<SyncResult> IAMTokenSyncer(const std::shared_ptr<GetResponse> &getResponse);

    litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID);
    litebus::Future<Status> VerifyToken(const std::string &token);
    litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID);

    void CheckTokenExpiredInAdvance();

    void ReplaceTokenMap(const std::unordered_map<std::string, std::shared_ptr<TokenSalt>> tokenMap)
    {
        newTokenMap_ = tokenMap;
    }

    void UpdateSyncToken(const std::vector<WatchEvent> &events);

    void RegisterUpdateTokenCallback(
        const std::function<void(const std::string &, const std::string &, const std::string &)> &cb)
    {
        updateTokenCallback_ = cb;
    }

    void BindMetaStorageAccessor(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor)
    {
        metaStorageAccessor_ = metaStorageAccessor;
    }

    void BindIAMClient(const std::shared_ptr<IAMClient> &iamClient)
    {
        iamClient_ = iamClient;
    }

private:
    void SyncPutToken(const std::string &tenantID, const std::string &tokenJson);
    void SyncDelToken(const std::string &tenantID);
    void SaveTokenAndTriggerUpdate(const std::string &tenantID, const std::shared_ptr<TokenSalt> &tokenSalt);
    void DoRequireEncryptToken(const std::string &tenantID,
                               const std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>> &promise,
                               const uint32_t retryTimes);
    litebus::Future<std::shared_ptr<TokenSalt>> OnRequireEncryptToken(
        const std::shared_ptr<TokenSalt> &tokenSalt, const std::string &tenantID,
        const std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>> &promise, const uint32_t retryTimes);
    void DoVerifyToken(const std::string &token, const std::shared_ptr<litebus::Promise<Status>> &promise,
                       const uint32_t retryTimes);

    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
    std::shared_ptr<IAMClient> iamClient_{ nullptr };
    std::function<void(const std::string &, const std::string &, const std::string &)> updateTokenCallback_{ nullptr };
    std::string clusterID_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>>> requirePending_;
    std::unordered_map<std::string, std::shared_ptr<TokenSalt>> newTokenMap_;
    litebus::Timer checkTokenExpiredInAdvanceTimer_;
};
} // namespace functionsystem::function_proxy
#endif // FUNCTION_PROXY_COMMON_IAM_INTERNAL_TOKEN_MANAGER_ACTOR_H