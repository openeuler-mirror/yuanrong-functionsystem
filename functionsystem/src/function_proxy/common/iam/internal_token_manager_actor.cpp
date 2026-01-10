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

#include "internal_token_manager_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/utils/meta_store_kv_operation.h"
#include "function_proxy/common/constants.h"

namespace functionsystem::function_proxy {

namespace {
bool IsValidToken(const std::shared_ptr<TokenSalt> &tokenSalt)
{
    auto now = static_cast<uint64_t>(std::time(nullptr));
    return tokenSalt->expiredTimeStamp > now + NEW_TOKEN_EXPIRED_OFFSET;
}
}  // namespace

Status InternalTokenManagerActor::Register()
{
    auto watchOpt = WatchOption{ true, true };
    auto prefix = GenTokenKeyWatchPrefix(clusterID_, true);
    YRLOG_INFO("register watch {}", prefix);
    ASSERT_IF_NULL(metaStorageAccessor_);
    auto syncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &InternalTokenManagerActor::IAMTokenSyncer, getResponse);
    };
    (void)metaStorageAccessor_->RegisterObserver(
        prefix, watchOpt,
        [aid(GetAID())](const std::vector<WatchEvent> &events, bool) {
            litebus::Async(aid, &InternalTokenManagerActor::UpdateSyncToken, events);
            return true;
        },
        syncer);
    CheckTokenExpiredInAdvance();
    return Status::OK();
}

void InternalTokenManagerActor::UpdateSyncToken(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto tenantID = GetTokenTenantID(eventKey);
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                SyncPutToken(tenantID, event.kv.value());
                break;
            }
            case EVENT_TYPE_DELETE: {
                SyncDelToken(tenantID);
                break;
            }
            default: {
                YRLOG_WARN("{}|unknown event type {}", tenantID, fmt::underlying(event.eventType));
                break;
            }
        }
    }
}

void InternalTokenManagerActor::SyncPutToken(const std::string &tenantID, const std::string &tokenJson)
{
    auto tokenSalt = TransTokenSaltFromJson(tokenJson);
    if (auto status(DecryptTokenSaltFromStorage(tokenSalt->token, tokenSalt)); status.IsError()) {
        YRLOG_WARN("{}|sync and put token failed, err: {}", tenantID, status.ToString());
        return;
    }
    SaveTokenAndTriggerUpdate(tenantID, tokenSalt);
}

void InternalTokenManagerActor::SyncDelToken(const std::string &tenantID)
{
    (void)newTokenMap_.erase(tenantID);
}

litebus::Future<SyncResult> InternalTokenManagerActor::IAMTokenSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    auto prefix = GenTokenKeyWatchPrefix(clusterID_, true);
    if (getResponse == nullptr) {
        YRLOG_INFO("null get response key({})", prefix);
        return SyncResult{ Status(StatusCode::FAILED) };
    }

    if (getResponse->status.IsError()) {
        YRLOG_INFO("failed to get key({}) from meta storage", prefix);
        return SyncResult{ getResponse->status };
    }
    if (getResponse->kvs.empty()) {
        YRLOG_INFO("get no result with key({}) from meta storage, revision is {}", prefix,
                   getResponse->header.revision);
        return SyncResult{ Status::OK() };
    }
    std::vector<WatchEvent> watchEvents;
    for (auto &kv : getResponse->kvs) {
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} };
        watchEvents.emplace_back(event);
    }
    UpdateSyncToken(watchEvents);
    return SyncResult{ Status::OK() };
}

litebus::Future<Status> InternalTokenManagerActor::VerifyToken(const std::string &token)
{
    for (auto &iter : newTokenMap_) {
        if (iter.second->token == token) {
            return Status::OK();
        }
    }
    auto promise = std::make_shared<litebus::Promise<Status>>();
    DoVerifyToken(token, promise, 0);
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<TokenSalt>> InternalTokenManagerActor::RequireEncryptToken(const std::string &tenantID)
{
    if (newTokenMap_.find(tenantID) != newTokenMap_.end() && IsValidToken(newTokenMap_[tenantID])) {
        if (IsValidToken(newTokenMap_[tenantID])) {
            return newTokenMap_[tenantID];
        }
        YRLOG_WARN("{}|The local token has expired or is about to expire: {}", tenantID,
                   newTokenMap_[tenantID]->expiredTimeStamp);
    }

    if (requirePending_.find(tenantID) != requirePending_.end()) {
        return requirePending_[tenantID]->GetFuture();
    }

    requirePending_[tenantID] = std::make_shared<litebus::Promise<std::shared_ptr<TokenSalt>>>();
    DoRequireEncryptToken(tenantID, requirePending_[tenantID], 0);
    return requirePending_[tenantID]->GetFuture();
}

void InternalTokenManagerActor::DoRequireEncryptToken(
    const std::string &tenantID, const std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>> &promise,
    const uint32_t retryTimes)
{
    ASSERT_IF_NULL(iamClient_);
    if (retryTimes > REQUEST_IAM_MAX_RETRY) {
        YRLOG_ERROR("{}|require token from iam exceed max times", tenantID);
        auto tokenSalt = std::make_shared<TokenSalt>();
        tokenSalt->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "require token timeout");
        requirePending_.erase(tenantID);
        promise->SetValue(tokenSalt);
        return;
    }
    YRLOG_INFO("{}|require token from iam, retryTimes({})", tenantID, retryTimes);
    (void)iamClient_->RequireEncryptToken(tenantID).Then(
        litebus::Defer(GetAID(), &InternalTokenManagerActor::OnRequireEncryptToken, std::placeholders::_1, tenantID,
                       promise, retryTimes));
}

litebus::Future<std::shared_ptr<TokenSalt>> InternalTokenManagerActor::OnRequireEncryptToken(
    const std::shared_ptr<TokenSalt> &tokenSalt, const std::string &tenantID,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<TokenSalt>>> &promise, const uint32_t retryTimes)
{
    if (tokenSalt->status.IsOk() || tokenSalt->status.StatusCode() != StatusCode::ERR_INNER_SYSTEM_ERROR) {
        if (!tokenSalt->token.empty()) {
            SaveTokenAndTriggerUpdate(tenantID, tokenSalt);
            YRLOG_INFO("{}|success to require token from iam", tenantID);
        } else {
            YRLOG_ERROR("{}|token required from iam server is empty", tenantID);
        }
        requirePending_.erase(tenantID);
        promise->SetValue(tokenSalt);
        return tokenSalt;
    }
    litebus::AsyncAfter(REQUEST_IAM_INTERVAL, GetAID(), &InternalTokenManagerActor::DoRequireEncryptToken, tenantID,
                        promise, retryTimes + 1);
    return tokenSalt;
}

void InternalTokenManagerActor::DoVerifyToken(const std::string &token,
                                              const std::shared_ptr<litebus::Promise<Status>> &promise,
                                              const uint32_t retryTimes)
{
    ASSERT_IF_NULL(iamClient_);
    if (retryTimes > REQUEST_IAM_MAX_RETRY) {
        promise->SetValue(Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "verify token timeout"));
        return;
    }
    YRLOG_INFO("verify token to iam-server, retryTimes({})", retryTimes);
    iamClient_->VerifyToken(token).Then([aid(GetAID()), token, promise, retryTimes](const Status status) {
        if (status.IsOk() || status.StatusCode() != StatusCode::ERR_INNER_SYSTEM_ERROR) {
            promise->SetValue(status);
            return Status::OK();
        }
        litebus::AsyncAfter(REQUEST_IAM_INTERVAL, aid, &InternalTokenManagerActor::DoVerifyToken, token, promise,
                            retryTimes + 1);
        return Status::OK();
    });
}

litebus::Future<Status> InternalTokenManagerActor::AbandonTokenByTenantID(const std::string &tenantID)
{
    if (newTokenMap_.find(tenantID) != newTokenMap_.end()) {
        CleanSensitiveStrMemory(newTokenMap_[tenantID]->token, "abandon token");
        (void)newTokenMap_.erase(tenantID);
    }
    return Status::OK();
}

void InternalTokenManagerActor::SaveTokenAndTriggerUpdate(const std::string &tenantID,
                                                          const std::shared_ptr<TokenSalt> &tokenSalt)
{
    if (newTokenMap_.find(tenantID) == newTokenMap_.end()) {
        (void)newTokenMap_.emplace(tenantID, std::make_shared<TokenSalt>(*tokenSalt));
        return;
    }
    newTokenMap_[tenantID]->token = tokenSalt->token;
    newTokenMap_[tenantID]->salt = tokenSalt->salt;
    newTokenMap_[tenantID]->expiredTimeStamp = tokenSalt->expiredTimeStamp;
    if (updateTokenCallback_ != nullptr) {
        updateTokenCallback_(tenantID, tokenSalt->token, tokenSalt->salt);
    }
}

void InternalTokenManagerActor::CheckTokenExpiredInAdvance()
{
    std::unordered_set<std::string> needUpdateSet;
    for (const auto &tokenIter : newTokenMap_) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (tokenIter.second->expiredTimeStamp < (now + TIME_AHEAD_OF_EXPIRED)) {
            needUpdateSet.insert(tokenIter.first);
        }
    }
    for (const auto &tenantID : needUpdateSet) {
        if (requirePending_.find(tenantID) != requirePending_.end()) {
            continue;
        }
        YRLOG_INFO("{}|start update token in advance", tenantID);

        requirePending_[tenantID] = std::make_shared<litebus::Promise<std::shared_ptr<TokenSalt>>>();
        DoRequireEncryptToken(tenantID, requirePending_[tenantID], 0);
    }
    checkTokenExpiredInAdvanceTimer_ = litebus::AsyncAfter(CHECK_EXPIRED_INTERVAL, GetAID(),
                                                           &InternalTokenManagerActor::CheckTokenExpiredInAdvance);
}
}  // namespace functionsystem::function_proxy