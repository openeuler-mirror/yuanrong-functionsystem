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

#ifndef FUNCTION_PROXY_COMMON_IAM_INTERNAL_CREDENTIAL_MANAGER_ACTOR_H
#define FUNCTION_PROXY_COMMON_IAM_INTERNAL_CREDENTIAL_MANAGER_ACTOR_H

#include <utility>

#include "actor/actor.hpp"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "iam_client.h"
#include "function_proxy/common/constants.h"

namespace functionsystem::function_proxy {
class InternalCredentialManagerActor : public litebus::ActorBase {
public:
    InternalCredentialManagerActor(const std::string &name, std::string clusterID)
        : litebus::ActorBase(name), clusterID_(std::move(clusterID)){};

    ~InternalCredentialManagerActor() override = default;

    Status Register();

    litebus::Future<SyncResult> IAMCredentialSyncer(const std::shared_ptr<GetResponse> &getResponse);

    litebus::Future<std::shared_ptr<AKSKContent>> RequireCredential(const std::string &tenantID);

    litebus::Future<std::shared_ptr<AKSKContent>> VerifyCredential(const std::string &accessKey);

    litebus::Future<Status> AbandonCredential(const std::string &tenantID);

    litebus::Future<bool> IsSystemTenant(const std::string &tenantID);

    void CheckCredentialExpiredInAdvance();

    void RegisterUpdateCallback(
        const std::function<void(const std::string &, const std::shared_ptr<AKSKContent> &)> &cb)
    {
        updateCallback_ = cb;
    }

    void BindMetaStorageAccessor(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor)
    {
        metaStorageAccessor_ = metaStorageAccessor;
    }

    void BindIAMClient(const std::shared_ptr<IAMClient> &iamClient)
    {
        iamClient_ = iamClient;
    }

protected:
    void Init() override;

private:
    void LoadSystemCredential();
    void LoadBuiltInCredential();

    size_t GetCredentialCount();  // just for test

    void SyncPutCredential(const std::string &tenantID, const std::string &credJson);
    void SyncDelCredential(const std::string &tenantID);
    void SaveCredentialAndTriggerUpdate(const std::string &tenantID, const std::shared_ptr<AKSKContent> &credential);

    void UpdateSyncCredential(const std::vector<WatchEvent> &events);

    void DoRequireCredential(const std::string &tenantID,
                             const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise,
                             const uint32_t retryTimes);

    litebus::Future<std::shared_ptr<EncAKSKContent>> OnRequireCredential(
        const std::shared_ptr<EncAKSKContent> &credential, const std::string &tenantID,
        const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const uint32_t retryTimes);

    void DoVerifyCredential(const std::string &accessKey,
                            const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise,
                            const uint32_t retryTimes);

    litebus::Future<std::shared_ptr<EncAKSKContent>> OnVerifyCredential(
        const std::shared_ptr<EncAKSKContent> &credential, const std::string &accessKey,
        const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const uint32_t retryTimes);

    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
    std::shared_ptr<IAMClient> iamClient_{ nullptr };
    std::function<void(const std::string &, const std::shared_ptr<AKSKContent> &)> updateCallback_{ nullptr };
    std::string clusterID_;
    std::unordered_map<std::string, std::shared_ptr<AKSKContent>> newCredMap_;
    std::unordered_map<std::string, std::string> accessKey2TenantIDMap_;
    litebus::Timer checkExpiredInAdvanceTimer_;
    uint32_t requestRetryInterval_ {REQUEST_IAM_INTERVAL};
    uint32_t requestMaxRetryTimes_ {REQUEST_IAM_MAX_RETRY};
};
}  // namespace functionsystem::function_proxy
#endif  // FUNCTION_PROXY_COMMON_IAM_INTERNAL_CREDENTIAL_MANAGER_ACTOR_H