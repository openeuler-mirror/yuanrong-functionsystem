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

#include "internal_credential_manager_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/utils/meta_store_kv_operation.h"
#include "function_proxy/common/constants.h"

namespace functionsystem::function_proxy {
void InternalCredentialManagerActor::Init()
{
    LoadSystemCredential();
    LoadBuiltInCredential();  // load build-in credential
    (void)litebus::Async(GetAID(), &InternalCredentialManagerActor::Register);
}

void InternalCredentialManagerActor::LoadSystemCredential()
{
    auto enable = litebus::os::GetEnv(litebus::os::LITEBUS_AKSK_ENABLED);
    if (enable.IsNone() || enable.Get() != "1") {
        return;
    }

    auto accessKey = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
    auto secretKey = litebus::os::GetEnv(litebus::os::LITEBUS_SECRET_KEY);
    auto dataKey = litebus::os::GetEnv(LITEBUS_DATA_KEY);
    if (accessKey.IsNone() || secretKey.IsNone() || dataKey.IsNone()) {
        YRLOG_ERROR("Credential's key is illegal when 2fa enabled");
        return;
    }

    std::shared_ptr<AKSKContent> credential = std::make_shared<AKSKContent>();
    credential->tenantID = "0";
    credential->role = SYSTEM_ROLE;

    credential->accessKey = accessKey.Get();
    credential->secretKey = SensitiveValue(secretKey.Get());
    credential->dataKey = SensitiveValue(dataKey.Get());

    newCredMap_[credential->tenantID] = credential;
    accessKey2TenantIDMap_[credential->accessKey] = credential->tenantID;
    YRLOG_INFO("{}|put system credential into credential map", credential->tenantID);
}

void InternalCredentialManagerActor::LoadBuiltInCredential()
{
    auto ciphertext = litebus::os::GetEnv(YR_BUILD_IN_CREDENTIAL);
    if (!ciphertext.IsSome() || ciphertext.Get().empty()) {
        return;
    }

    SensitiveValue dataKey = GetComponentDataKey();
    if (dataKey.Empty()) {
        YRLOG_ERROR("no dataKey to decrypt built-in credential.");
        return;
    }

    SensitiveValue plain(ciphertext.Get());
    auto credential = TransToAKSKContentFromJson(plain.GetData());
    if (credential->IsValid().IsError()) {
        YRLOG_ERROR("failed to parse built-in credential: {}", credential->status.GetMessage());
        return;
    }

    credential->role = NORMAL_ROLE;
    newCredMap_[credential->tenantID] = credential;
    accessKey2TenantIDMap_[credential->accessKey] = credential->tenantID;
    YRLOG_INFO("{}|put built-in credential: {} into credential map", credential->tenantID,
               credential->secretKey.GetMaskData());
}

size_t InternalCredentialManagerActor::GetCredentialCount()
{
    return newCredMap_.size();
}

Status InternalCredentialManagerActor::Register()
{
    auto watchOpt = WatchOption{ true, true };
    auto prefix = GenAKSKKeyWatchPrefix(clusterID_, true);
    YRLOG_INFO("register watch {}", prefix);
    ASSERT_IF_NULL(metaStorageAccessor_);
    auto syncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &InternalCredentialManagerActor::IAMCredentialSyncer, getResponse);
    };
    (void)metaStorageAccessor_->RegisterObserver(
        prefix, watchOpt,
        [aid(GetAID())](const std::vector<WatchEvent> &events, bool) {
            litebus::Async(aid, &InternalCredentialManagerActor::UpdateSyncCredential, events);
            return true;
        },
        syncer);
    CheckCredentialExpiredInAdvance();
    return Status::OK();
}

void InternalCredentialManagerActor::UpdateSyncCredential(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto tenantID = GetAKSKTenantID(eventKey);
        YRLOG_DEBUG("detected credential updated, tenantID: {}, eventType:{}", tenantID,
                    fmt::underlying(event.eventType));
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                SyncPutCredential(tenantID, event.kv.value());
                break;
            }
            case EVENT_TYPE_DELETE: {
                SyncDelCredential(tenantID);
                break;
            }
            default: {
                YRLOG_WARN("unknown event type {}", fmt::underlying(event.eventType));
                break;
            }
        }
    }
}

void InternalCredentialManagerActor::SyncPutCredential(const std::string &tenantID, const std::string &credJson)
{
    auto encCredential = TransToEncAKSKContentFromJson(credJson);
    if (encCredential->status.IsError()) {
        return;
    }
    auto credentialContent = DecryptAKSKContentFromStorage(encCredential);
    if (credentialContent->status.IsError()) {
        return;
    }
    SaveCredentialAndTriggerUpdate(tenantID, credentialContent);
}

void InternalCredentialManagerActor::SyncDelCredential(const std::string &tenantID)
{
    if (newCredMap_.find(tenantID) != newCredMap_.end()) {
        auto accessKey = newCredMap_[tenantID]->accessKey;
        (void)accessKey2TenantIDMap_.erase(accessKey);
    }
    (void)newCredMap_.erase(tenantID);
}

litebus::Future<SyncResult> InternalCredentialManagerActor::IAMCredentialSyncer(
    const std::shared_ptr<GetResponse> &getResponse)
{
    auto prefix = GenAKSKKeyWatchPrefix(clusterID_, true);
    if (getResponse->status.IsError()) {
        YRLOG_WARN("failed to get key({}) from meta storage", prefix);
        return SyncResult{ getResponse->status };
    }
    if (getResponse->kvs.empty()) {
        YRLOG_WARN("get no result with key({}) from meta storage, revision is {}", prefix,
                   getResponse->header.revision);
        return SyncResult{ Status::OK() };
    }
    std::vector<WatchEvent> watchEvents;
    for (auto &kv : getResponse->kvs) {
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} };
        watchEvents.emplace_back(event);
    }
    UpdateSyncCredential(watchEvents);
    return SyncResult{ Status::OK() };
}

litebus::Future<std::shared_ptr<AKSKContent>> InternalCredentialManagerActor::RequireCredential(
    const std::string &tenantID)
{
    if (newCredMap_.find(tenantID) != newCredMap_.end()
        && newCredMap_[tenantID]->IsValid(NEW_CREDENTIAL_EXPIRED_OFFSET)) {
        YRLOG_DEBUG("require encrypt credential return local, tenantID: {}", tenantID);
        return newCredMap_[tenantID];
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<AKSKContent>>>();
    DoRequireCredential(tenantID, promise, 0);
    return promise->GetFuture();
}

void InternalCredentialManagerActor::DoRequireCredential(
    const std::string &tenantID, const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise,
    const uint32_t retryTimes)
{
    ASSERT_IF_NULL(iamClient_);
    if (retryTimes > requestMaxRetryTimes_) {
        YRLOG_ERROR("{}|require encrypt credential from iam exceed max times", tenantID);
        auto credential = std::make_shared<AKSKContent>();
        credential->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "require credential timeout");
        promise->SetValue(credential);
        return;
    }
    YRLOG_INFO("{}|require credential from iam, retryTimes({})", tenantID, retryTimes);
    (void)iamClient_->RequireEncryptCredential(tenantID).Then(
        litebus::Defer(GetAID(), &InternalCredentialManagerActor::OnRequireCredential, std::placeholders::_1, tenantID,
                       promise, retryTimes));
}

litebus::Future<std::shared_ptr<EncAKSKContent>> InternalCredentialManagerActor::OnRequireCredential(
    const std::shared_ptr<EncAKSKContent> &credential, const std::string &tenantID,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const uint32_t retryTimes)
{
    if (credential->status.StatusCode() == StatusCode::ERR_INNER_SYSTEM_ERROR) {
        litebus::AsyncAfter(requestRetryInterval_, GetAID(), &InternalCredentialManagerActor::DoRequireCredential,
                            tenantID, promise, retryTimes + 1);
        return credential;
    }
    if (credential->status.IsError()) {
        auto errContent = std::make_shared<AKSKContent>();
        errContent->status = credential->status;
        promise->SetValue(errContent);
        return credential;
    }
    auto credentialContent = DecryptAKSKContentFromStorage(credential);
    credentialContent->status = credentialContent->IsValid();
    if (credentialContent->status.IsOk()) {
        SaveCredentialAndTriggerUpdate(tenantID, credentialContent);
    }
    promise->SetValue(credentialContent);
    return credential;
}

void InternalCredentialManagerActor::SaveCredentialAndTriggerUpdate(const std::string &tenantID,
                                                                    const std::shared_ptr<AKSKContent> &credential)
{
    if (newCredMap_.find(tenantID) == newCredMap_.end()) {
        YRLOG_INFO("{}|put credential: {} into map", credential->tenantID, credential->secretKey.GetMaskData());
        (void)newCredMap_.emplace(tenantID, std::make_shared<AKSKContent>(*credential));  // add
        accessKey2TenantIDMap_[credential->accessKey] = tenantID;
        return;
    }

    if (credential->expiredTimeStamp < newCredMap_[tenantID]->expiredTimeStamp) {
        YRLOG_WARN("{}|save credential, expired:{} but cache is {}, skip update", tenantID,
                   credential->expiredTimeStamp, newCredMap_[tenantID]->expiredTimeStamp);
        return;
    }

    YRLOG_INFO("{}|put credential: {} into map", credential->tenantID, credential->secretKey.GetMaskData());
    accessKey2TenantIDMap_.erase(newCredMap_[tenantID]->accessKey);
    newCredMap_[tenantID] = credential->Copy();  // replace
    accessKey2TenantIDMap_[credential->accessKey] = tenantID;
    if (updateCallback_ != nullptr) {
        updateCallback_(tenantID, credential);
    }
}

litebus::Future<std::shared_ptr<AKSKContent>> InternalCredentialManagerActor::VerifyCredential(
    const std::string &accessKey)
{
    if (accessKey2TenantIDMap_.find(accessKey) != accessKey2TenantIDMap_.end()) {
        auto tenantID = accessKey2TenantIDMap_[accessKey];
        if (newCredMap_.find(tenantID) != newCredMap_.end() && newCredMap_[tenantID]->IsValid().IsOk()) {
            return newCredMap_[tenantID];
        }
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<AKSKContent>>>();
    DoVerifyCredential(accessKey, promise, 0);
    return promise->GetFuture();
}

void InternalCredentialManagerActor::DoVerifyCredential(
    const std::string &accessKey, const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise,
    const uint32_t retryTimes)
{
    ASSERT_IF_NULL(iamClient_);
    if (retryTimes > requestMaxRetryTimes_) {
        YRLOG_ERROR("verify credential from iam exceed max times");
        auto errCredential = std::make_shared<AKSKContent>();
        errCredential->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "verify credential timeout");
        promise->SetValue(errCredential);
        return;
    }
    YRLOG_INFO("verify credential from iam, retryTimes({})", retryTimes);
    (void)iamClient_->VerifyCredential(accessKey).Then(
        litebus::Defer(GetAID(), &InternalCredentialManagerActor::OnVerifyCredential, std::placeholders::_1, accessKey,
                       promise, retryTimes));
}

litebus::Future<std::shared_ptr<EncAKSKContent>> InternalCredentialManagerActor::OnVerifyCredential(
    const std::shared_ptr<EncAKSKContent> &credential, const std::string &accessKey,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<AKSKContent>>> &promise, const uint32_t retryTimes)
{
    if (credential->status.StatusCode() == StatusCode::ERR_INNER_SYSTEM_ERROR) {
        litebus::AsyncAfter(requestRetryInterval_, GetAID(), &InternalCredentialManagerActor::DoVerifyCredential,
                            accessKey, promise, retryTimes + 1);
        return credential;
    }
    if (credential->status.IsError()) {
        auto errContent = std::make_shared<AKSKContent>();
        errContent->status = credential->status;
        promise->SetValue(errContent);
        return credential;
    }
    auto credentialContent = DecryptAKSKContentFromStorage(credential);
    credentialContent->status = credentialContent->IsValid();
    if (credentialContent->status.IsOk()) {
        SaveCredentialAndTriggerUpdate(credential->tenantID, credentialContent);
    }
    promise->SetValue(credentialContent);
    return credential;
}

litebus::Future<Status> InternalCredentialManagerActor::AbandonCredential(const std::string &tenantID)
{
    if (const auto it = newCredMap_.find(tenantID); it != newCredMap_.end()) {
        if (it->second->expiredTimeStamp == 0) {
            // 永久凭据不删除，runtime重启时复用
            return Status::OK();
        }

        const auto accessKey = newCredMap_[tenantID]->accessKey;
        (void)accessKey2TenantIDMap_.erase(accessKey);
        (void)newCredMap_.erase(tenantID);
    }
    return Status::OK();
}

litebus::Future<bool> InternalCredentialManagerActor::IsSystemTenant(const std::string &tenantID)
{
    return RequireCredential(tenantID).Then(std::function<litebus::Future<bool>(const std::shared_ptr<AKSKContent> &)>(
        [tenantID](const std::shared_ptr<AKSKContent> &akskContent) -> litebus::Future<bool> {
            if (akskContent == nullptr) {
                YRLOG_ERROR("failed to check tenant({}) is system or not", tenantID);
                return false;
            }
            return akskContent->role == SYSTEM_ROLE;
        }));
}

void InternalCredentialManagerActor::CheckCredentialExpiredInAdvance()
{
    std::unordered_set<std::string> needUpdateSet;
    for (const auto &iter : newCredMap_) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (iter.second->expiredTimeStamp != 0 && iter.second->expiredTimeStamp < (now + TIME_AHEAD_OF_EXPIRED)) {
            needUpdateSet.insert(iter.first);
        }
    }
    for (const auto &val : needUpdateSet) {
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<AKSKContent>>>();
        DoRequireCredential(val, promise, 0);
    }
    checkExpiredInAdvanceTimer_ = litebus::AsyncAfter(CHECK_EXPIRED_INTERVAL, GetAID(),
                                                      &InternalCredentialManagerActor::CheckCredentialExpiredInAdvance);
}
}  // namespace functionsystem::function_proxy
