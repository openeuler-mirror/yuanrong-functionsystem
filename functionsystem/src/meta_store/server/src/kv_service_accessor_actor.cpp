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

#include "kv_service_accessor_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/proto/pb/message_pb.h"
#include "kv_service_actor.h"
#include "meta_store_common.h"

namespace functionsystem::meta_store {
KvServiceAccessorActor::KvServiceAccessorActor(const litebus::AID &kvServiceActor)
    : ActorBase("KvServiceAccessorActor"), kvServiceActor_(kvServiceActor)
{
}

KvServiceAccessorActor::KvServiceAccessorActor(const litebus::AID &kvServiceActor, const std::string &namePrefix)
    : ActorBase(namePrefix + "KvServiceAccessorActor"), kvServiceActor_(kvServiceActor)
{
}

void KvServiceAccessorActor::Init()
{
    Receive("Put", &KvServiceAccessorActor::AsyncPut);
    Receive("Delete", &KvServiceAccessorActor::AsyncDelete);
    Receive("Get", &KvServiceAccessorActor::AsyncGet);
    Receive("Txn", &KvServiceAccessorActor::AsyncTxn);
    Receive("Watch", &KvServiceAccessorActor::AsyncWatch);
    Receive("GetAndWatch", &KvServiceAccessorActor::AsyncGetAndWatch);

    isRecoverReady_ = litebus::Async(kvServiceActor_, &KvServiceActor::Recover);
}

void KvServiceAccessorActor::Finalize()
{
}

void KvServiceAccessorActor::AsyncWatch(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::MetaStoreRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("illegal watch request");
        return;
    }
    if (!InsertRequestSet(req->requestid())) {
        YRLOG_INFO("{}|Received duplicate watch request from {}", req->requestid(), from.HashString());
        return;
    }
    YRLOG_DEBUG("{}|receive watch request from {}", req->requestid(), from.HashString());

    if (isRecoverReady_.IsInit()) {
        isRecoverReady_.Then(litebus::Defer(kvServiceActor_, &KvServiceActor::AsyncWatch, from, req))
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    } else {
        litebus::Async(kvServiceActor_, &KvServiceActor::AsyncWatch, from, req)
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    }
}

void KvServiceAccessorActor::AsyncGetAndWatch(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::MetaStoreRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("illegal get and watch request");
        return;
    }
    if (!InsertRequestSet(req->requestid())) {
        YRLOG_INFO("{}|Received duplicate get and watch request from {}", req->requestid(), from.HashString());
        return;
    }
    YRLOG_DEBUG("{}|receive get and watch request from {}", req->requestid(), from.HashString());

    if (isRecoverReady_.IsInit()) {
        isRecoverReady_.Then(litebus::Defer(kvServiceActor_, &KvServiceActor::AsyncGetAndWatch, from, req))
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    } else {
        litebus::Async(kvServiceActor_, &KvServiceActor::AsyncGetAndWatch, from, req)
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    }
}

void KvServiceAccessorActor::AsyncPut(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::MetaStore::PutRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("receive illegal put request");
        return;
    }
    if (!InsertRequestSet(req->requestid())) {
        YRLOG_INFO("{}|Received duplicate put request from {}", req->requestid(), from.HashString());
        return;
    }
    YRLOG_DEBUG("{}|receive put request from {}", req->requestid(), from.HashString());

    if (isRecoverReady_.IsInit()) {
        isRecoverReady_.Then(litebus::Defer(kvServiceActor_, &KvServiceActor::AsyncPut, from, req))
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    } else {
        litebus::Async(kvServiceActor_, &KvServiceActor::AsyncPut, from, req)
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    }
}

void KvServiceAccessorActor::AsyncDelete(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::MetaStoreRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("receive illegal delete request");
        return;
    }
    if (!InsertRequestSet(req->requestid())) {
        YRLOG_INFO("{}|Received duplicate delete request from {}", req->requestid(), from.HashString());
        return;
    }
    YRLOG_DEBUG("{}|receive delete request from {}", req->requestid(), from.HashString());

    if (isRecoverReady_.IsInit()) {
        isRecoverReady_.Then(litebus::Defer(kvServiceActor_, &KvServiceActor::AsyncDelete, from, req))
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    } else {
        litebus::Async(kvServiceActor_, &KvServiceActor::AsyncDelete, from, req)
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    }
}

void KvServiceAccessorActor::AsyncGet(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::MetaStoreRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("receive illegal get request.");
        return;
    }
    if (!InsertRequestSet(req->requestid())) {
        YRLOG_INFO("{}|Received duplicate get request from {}", req->requestid(), from.HashString());
        return;
    }
    YRLOG_DEBUG("{}|receive get request from {}", req->requestid(), from.HashString());

    if (isRecoverReady_.IsInit()) {
        isRecoverReady_.Then(litebus::Defer(kvServiceActor_, &KvServiceActor::AsyncGet, from, req))
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    } else {
        litebus::Async(kvServiceActor_, &KvServiceActor::AsyncGet, from, req)
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    }
}

void KvServiceAccessorActor::AsyncTxn(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::MetaStoreRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("receive illegal txn request");
        return;
    }
    if (!InsertRequestSet(req->requestid())) {
        YRLOG_INFO("{}|Received duplicate txn request from {}", req->requestid(), from.HashString());
        return;
    }
    YRLOG_DEBUG("{}|receive txn request from {}", req->requestid(), from.HashString());

    if (isRecoverReady_.IsInit()) {
        isRecoverReady_.Then(litebus::Defer(kvServiceActor_, &KvServiceActor::AsyncTxn, from, req))
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    } else {
        litebus::Async(kvServiceActor_, &KvServiceActor::AsyncTxn, from, req)
            .OnComplete(litebus::Defer(GetAID(), &KvServiceAccessorActor::RemoveRequestSet, req->requestid()));
    }
}

bool KvServiceAccessorActor::InsertRequestSet(const std::string &id)
{
    auto success = requestSet_.emplace(id).second;
    return success;
}

void KvServiceAccessorActor::RemoveRequestSet(const std::string &id)
{
    if (auto iter(requestSet_.find(id)); iter != requestSet_.end()) {
        requestSet_.erase(iter);
    }
}
}  // namespace functionsystem::meta_store
