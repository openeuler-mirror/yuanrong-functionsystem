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

#ifndef FUNCTION_PROXY_COMMON_IAM_INTERNAL_IAM_H
#define FUNCTION_PROXY_COMMON_IAM_INTERNAL_IAM_H

#include "async/future.hpp"
#include "authorize_proxy.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/status/status.h"
#include "common/utils/aksk_content.h"
#include "common/utils/token_transfer.h"
#include "iam_client.h"
#include "internal_token_manager_actor.h"
#include "internal_credential_manager_actor.h"

namespace functionsystem::function_proxy {
class VerifiedInstance;
enum class IAMCredType : uint32_t { TOKEN = 0, AK_SK = 1 };
class InternalIAM {
public:
    struct Param {
        bool isEnableIAM;
        std::string policyPath;
        std::string clusterID;
        IAMCredType credType{ IAMCredType::TOKEN };
    };
    explicit InternalIAM(const Param &param) : startParam_(std::move(param)){};
    virtual ~InternalIAM();

    virtual bool IsIAMEnabled();

    virtual IAMCredType GetCredType();

    bool Init();

    // set public for test
    virtual litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID);
    virtual litebus::Future<Status> VerifyToken(const std::string &token);
    virtual litebus::Future<Status> AbandonTokenByTenantID(const std::string &tenantID);
    virtual Status Authorize(AuthorizeParam &authorizeParam);

    virtual litebus::Future<std::shared_ptr<AKSKContent>> RequireCredentialByTenantID(const std::string &tenantID);
    virtual litebus::Future<std::shared_ptr<AKSKContent>> RequireCredentialByAK(const std::string &accessKey);
    virtual litebus::Future<Status> AbandonCredentialByTenantID(const std::string &tenantID);

    virtual litebus::Future<bool> IsSystemTenant(const std::string &tenantID);

    void RegisterUpdateTokenCallback(
        const std::function<void(const std::string &, const std::string &, const std::string &)> &cb);

    void RegisterUpdateCredentialCallback(
        const std::function<void(const std::string &, const std::shared_ptr<AKSKContent> &)> &cb);

    void BindMetaStorageAccessor(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor)
    {
        metaStorageAccessor_ = metaStorageAccessor;
    }

    void BindIAMClient(const std::shared_ptr<IAMClient> &iamClient)
    {
        iamClient_ = iamClient;
    }

    void Insert(const std::string &instanceID);
    void Delete(const std::string &instanceID);
    bool VerifyInstance(const std::string &instanceID);

    [[maybe_unused]] size_t GetVerifiedInstanceSize() const;

private:
    std::unordered_map<std::string, std::shared_ptr<TokenSalt>> SyncTokenFromMetaStore();

    std::unordered_map<std::string, std::shared_ptr<AKSKContent>> SyncCredentialFromMetaStore();

    Param startParam_;
    std::shared_ptr<AuthorizeProxy> authorizeProxy_{ nullptr };
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
    std::shared_ptr<VerifiedInstance> verifiedInstance_ = std::make_shared<VerifiedInstance>();
    std::shared_ptr<IAMClient> iamClient_{ nullptr };
    std::shared_ptr<InternalTokenManagerActor> syncTokenActor_{ nullptr };
    std::shared_ptr<InternalCredentialManagerActor> credentialActor_{nullptr};
};

class VerifiedInstance {
public:
    VerifiedInstance() = default;
    ~VerifiedInstance() = default;

    void Insert(const std::string &instanceID);
    void Delete(const std::string &instanceID);
    bool VerifyInstance(const std::string &instanceID);

    [[maybe_unused]] inline size_t GetVerifiedInstanceSize() const
    {
        return instances_.size();
    }
private:
    std::mutex mtx_;
    std::unordered_set<std::string> instances_ {};
};
}  // namespace functionsystem
#endif  // FUNCTION_PROXY_COMMON_IAM_INTERNAL_IAM_H