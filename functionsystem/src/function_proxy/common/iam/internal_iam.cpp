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

#include "internal_iam.h"

#include "common/logs/logging.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/utils/token_transfer.h"

namespace functionsystem::function_proxy {

InternalIAM::~InternalIAM()
{
    if (syncTokenActor_ != nullptr) {
        litebus::Terminate(syncTokenActor_->GetAID());
        litebus::Await(syncTokenActor_->GetAID());
    }
    if (credentialActor_ != nullptr) {
        litebus::Terminate(credentialActor_->GetAID());
        litebus::Await(credentialActor_->GetAID());
    }
}

bool InternalIAM::IsIAMEnabled()
{
    return startParam_.isEnableIAM;
}

IAMCredType InternalIAM::GetCredType()
{
    return startParam_.credType;
}

bool InternalIAM::Init()
{
    if (!startParam_.isEnableIAM) {
        YRLOG_WARN("internalIAM is not enable, Init failed.");
        return false;
    }
    auto realPolicy = litebus::os::RealPath(startParam_.policyPath);
    if (realPolicy.IsNone()) {
        YRLOG_WARN("policyPath({}) is invalid, disable iam.", startParam_.policyPath);
        startParam_.isEnableIAM = false;
        return false;
    }
    startParam_.policyPath = realPolicy.Get();
    authorizeProxy_ = std::make_shared<AuthorizeProxy>(startParam_.policyPath);
    if (auto status(authorizeProxy_->UpdatePolicy()); status.IsError()) {
        YRLOG_WARN("UpdatePolicy failed, err: {}", status.ToString());
    }
    if (startParam_.credType == IAMCredType::TOKEN) {
        auto tokenMap = SyncTokenFromMetaStore();
        auto actorName = "InternalTokenManagerActor_"  + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        syncTokenActor_ = std::make_shared<InternalTokenManagerActor>(actorName, startParam_.clusterID);
        syncTokenActor_->BindMetaStorageAccessor(metaStorageAccessor_);
        syncTokenActor_->BindIAMClient(iamClient_);
        syncTokenActor_->ReplaceTokenMap(tokenMap);
        (void)litebus::Spawn(syncTokenActor_);
        (void)litebus::Async(syncTokenActor_->GetAID(), &InternalTokenManagerActor::Register);
    } else {
        auto actorName = "InternalCredentialManagerActor_"  + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        credentialActor_ = std::make_shared<InternalCredentialManagerActor>(actorName, startParam_.clusterID);
        credentialActor_->BindMetaStorageAccessor(metaStorageAccessor_);
        credentialActor_->BindIAMClient(iamClient_);
        (void)litebus::Spawn(credentialActor_);
    }
    YRLOG_DEBUG("InternalIAM init success");
    return true;
}

std::unordered_map<std::string, std::shared_ptr<TokenSalt>> InternalIAM::SyncTokenFromMetaStore()
{
    ASSERT_IF_NULL(metaStorageAccessor_);
    std::unordered_map<std::string, std::shared_ptr<TokenSalt>> tokenMap;
    auto syncResult = metaStorageAccessor_->Sync(GenTokenKeyWatchPrefix(startParam_.clusterID, true), true);
    for (const auto &event : syncResult.first) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto tokenSalt = TransTokenSaltFromJson(event.kv.value());
        if (auto status(DecryptTokenSaltFromStorage(tokenSalt->token, tokenSalt)); status.IsError()) {
            YRLOG_WARN("sync encrypt token from storage failed, err: {}", status.ToString());
            continue;
        }
        auto tenantID = GetTokenTenantID(eventKey);
        (void)tokenMap.emplace(tenantID, std::make_shared<TokenSalt>(*tokenSalt));
        YRLOG_INFO("{}|put token into newTokenMap and verifiedToken", tenantID);
    }
    return tokenMap;
}

std::unordered_map<std::string, std::shared_ptr<AKSKContent>> InternalIAM::SyncCredentialFromMetaStore()
{
    ASSERT_IF_NULL(metaStorageAccessor_);
    std::unordered_map<std::string, std::shared_ptr<AKSKContent>> credentialMap;
    auto syncResult = metaStorageAccessor_->Sync(GenAKSKKeyWatchPrefix(startParam_.clusterID, true), true);
    for (const auto &event : syncResult.first) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto encAkSk = TransToEncAKSKContentFromJson(event.kv.value());
        if (encAkSk->status.IsError()) {
            continue;
        }
        auto akSk = DecryptAKSKContentFromStorage(encAkSk);
        if (akSk->IsValid()) {
            auto tenantID = GetAKSKTenantID(eventKey);
            credentialMap[tenantID] = akSk;
            YRLOG_INFO("{}|put token into newTokenMap and verifiedToken", tenantID);
        }
    }
    return credentialMap;
}

litebus::Future<std::shared_ptr<TokenSalt>> InternalIAM::RequireEncryptToken(const std::string &tenantID)
{
    if (syncTokenActor_ == nullptr) {
        auto tokenSalt = std::make_shared<TokenSalt>();
        tokenSalt->status = Status(StatusCode::FAILED, "token operate is not support");
        return tokenSalt;
    }
    return litebus::Async(syncTokenActor_->GetAID(), &InternalTokenManagerActor::RequireEncryptToken, tenantID);
}

litebus::Future<Status> InternalIAM::VerifyToken(const std::string &token)
{
    if (syncTokenActor_ == nullptr) {
        return Status::OK();
    }
    return litebus::Async(syncTokenActor_->GetAID(), &InternalTokenManagerActor::VerifyToken, token);
}

Status InternalIAM::Authorize(AuthorizeParam &authorizeParam)
{
    ASSERT_IF_NULL(authorizeProxy_);
    return authorizeProxy_->Authorize(authorizeParam);
}

litebus::Future<Status> InternalIAM::AbandonTokenByTenantID(const std::string &tenantID)
{
    if (syncTokenActor_ == nullptr) {
        return Status::OK();
    }
    return litebus::Async(syncTokenActor_->GetAID(), &InternalTokenManagerActor::AbandonTokenByTenantID, tenantID);
}

litebus::Future<std::shared_ptr<AKSKContent>> InternalIAM::RequireCredentialByTenantID(const std::string &tenantID)
{
    if (credentialActor_ == nullptr) {
        auto content = std::make_shared<AKSKContent>();
        content->status = Status(StatusCode::FAILED, "AK/SK operate is not support");
        return content;
    }
    return litebus::Async(credentialActor_->GetAID(), &InternalCredentialManagerActor::RequireCredential, tenantID);
}
litebus::Future<std::shared_ptr<AKSKContent>> InternalIAM::RequireCredentialByAK(const std::string &accessKey)
{
    if (credentialActor_ == nullptr) {
        auto content = std::make_shared<AKSKContent>();
        content->status = Status(StatusCode::FAILED, "AK/SK operate is not support");
        return content;
    }
    return litebus::Async(credentialActor_->GetAID(), &InternalCredentialManagerActor::VerifyCredential, accessKey);
}

litebus::Future<Status> InternalIAM::AbandonCredentialByTenantID(const std::string &tenantID)
{
    if (credentialActor_ == nullptr) {
        return Status::OK();
    }
    return litebus::Async(credentialActor_->GetAID(), &InternalCredentialManagerActor::AbandonCredential, tenantID);
}

litebus::Future<bool> InternalIAM::IsSystemTenant(const std::string &tenantID)
{
    if (credentialActor_ != nullptr) {
        return litebus::Async(credentialActor_->GetAID(), &InternalCredentialManagerActor::IsSystemTenant, tenantID);
    }

    // if ak sk is disabled, only tenant 0 is system function
    return tenantID == "0";
}

void InternalIAM::RegisterUpdateTokenCallback(
    const std::function<void(const std::string &, const std::string &, const std::string &)> &cb)
{
    if (syncTokenActor_ != nullptr) {
        litebus::Async(syncTokenActor_->GetAID(), &InternalTokenManagerActor::RegisterUpdateTokenCallback, cb);
    }
}

void InternalIAM::RegisterUpdateCredentialCallback(
    const std::function<void(const std::string &, const std::shared_ptr<AKSKContent> &)> &cb)
{
    if (credentialActor_ != nullptr) {
        litebus::Async(credentialActor_->GetAID(), &InternalCredentialManagerActor::RegisterUpdateCallback, cb);
    }
}

void InternalIAM::Insert(const std::string &instanceID)
{
    return verifiedInstance_->Insert(instanceID);
}

void InternalIAM::Delete(const std::string &instanceID)
{
    return verifiedInstance_->Delete(instanceID);
}

bool InternalIAM::VerifyInstance(const std::string &instanceID)
{
    return verifiedInstance_->VerifyInstance(instanceID);
}

size_t InternalIAM::GetVerifiedInstanceSize() const
{
    return verifiedInstance_->GetVerifiedInstanceSize();
}

void VerifiedInstance::Insert(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(mtx_);
    (void)instances_.insert(instanceID);
}

void VerifiedInstance::Delete(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(mtx_);
    (void)instances_.erase(instanceID);
}

bool VerifiedInstance::VerifyInstance(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(mtx_);
    return instances_.find(instanceID) != instances_.end();
}
}  // namespace functionsystem::function_proxy