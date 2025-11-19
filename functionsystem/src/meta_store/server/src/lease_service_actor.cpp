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

#include "lease_service_actor.h"

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "backup_actor.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "kv_service_actor.h"
#include "meta_store_common.h"

namespace functionsystem::meta_store {

constexpr uint64_t LEASE_WAIT_TIME_MS = 500;
constexpr int64_t MILLISECONDS_PRE_SECOND = 1000;

const std::string META_STORE_BACKUP_LEASE_PREFIX = "/metastore/lease/";

LeaseServiceActor::LeaseServiceActor(const litebus::AID &kvServiceActor, bool needExplore,
                                     const litebus::AID &backupActor)
    : ActorBase("LeaseServiceActor"),
      kvServiceActor_(kvServiceActor),
      backupActor_(backupActor),
      running_(false),
      needExplore_(needExplore)

{
    if (!backupActor_.OK()) {
        needExplore_ = false;
    }
}

LeaseServiceActor::LeaseServiceActor(const litebus::AID &kvServiceActor, const std::string &namePrefix)
    : ActorBase(namePrefix + "LeaseServiceActor"), kvServiceActor_(kvServiceActor), running_(false), needExplore_(false)
{
}

LeaseServiceActor::~LeaseServiceActor() = default;

void LeaseServiceActor::Init()
{
    Receive("ReceiveGrant", &LeaseServiceActor::ReceiveGrant);
    Receive("ReceiveRevoke", &LeaseServiceActor::ReceiveRevoke);
    Receive("ReceiveKeepAliveOnce", &LeaseServiceActor::ReceiveKeepAlive);
    InitExplorer();
}

bool LeaseServiceActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    if (!needExplore_) {
        return true;
    }

    litebus::AID masterAID("LeaseServiceActor", leaderInfo.address);
    member_->leaderInfo = leaderInfo;

    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("LeaseServiceActor UpdateLeaderInfo new status({}) business don't exist", newStatus);
        return false;
    }
    business_ = businesses_[newStatus];
    ASSERT_IF_NULL(business_);
    curStatus_ = newStatus;
    business_->OnChange();
    return true;
}

void LeaseServiceActor::InitExplorer()
{
    member_ = std::make_shared<Member>();
    auto slaveBusiness = std::make_shared<SlaveBusiness>(member_, shared_from_this());
    auto masterBusiness = std::make_shared<MasterBusiness>(member_, shared_from_this());

    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);
    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);

    if (needExplore_) {
        business_ = businesses_[SLAVE_BUSINESS];
        curStatus_ = SLAVE_BUSINESS;
        (void)explorer::Explorer::GetInstance().AddLeaderChangedCallback(
            "leaseService", [aid(GetAID())](const explorer::LeaderInfo &leaderInfo) {
                litebus::Async(aid, &LeaseServiceActor::UpdateLeaderInfo, leaderInfo);
            });
        return;
    }

    // don't explore, set to master business
    curStatus_ = MASTER_BUSINESS;
    business_ = businesses_[MASTER_BUSINESS];
}

Status LeaseServiceActor::Start()
{
    GetOption option;
    option.prefix = true;

    if (!backupActor_.OK()) {
        running_ = true;
        litebus::Async(GetAID(), &LeaseServiceActor::CheckpointScheduledLeases);
        return Status::OK();
    }
    litebus::Async(backupActor_, &BackupActor::Get, META_STORE_BACKUP_LEASE_PREFIX, option)
        .Then(litebus::Defer(GetAID(), &LeaseServiceActor::Sync, std::placeholders::_1));
    return Status::OK();
}

Status LeaseServiceActor::Stop()
{
    running_ = false;
    return Status::OK();
}

bool LeaseServiceActor::Sync(const std::shared_ptr<GetResponse> &getResponse)
{
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    for (const auto &item : getResponse->kvs) {
        ::messages::Lease lease;
        if (!lease.ParseFromString(item.value())) {
            YRLOG_WARN("failed to parse value for lease({})", item.key());
            continue;
        }
        if (lease.ttl() * MILLISECONDS_PRE_SECOND > INT64_MAX - milliseconds) {
            lease.set_expiry(0);
        } else {
            // update expiry
            lease.set_expiry(milliseconds + (lease.ttl() * MILLISECONDS_PRE_SECOND));
        }
        leases_[lease.id()] = lease;
        YRLOG_INFO("success to sync lease({})", lease.id());
    }
    YRLOG_INFO("success to sync leases");
    running_ = true;
    litebus::Async(GetAID(), &LeaseServiceActor::CheckpointScheduledLeases);

    if (needExplore_) {
        WatchOption option;
        option.prefix = true;
        option.revision = getResponse->header.revision;

        auto kvSyncer =
            [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
            return litebus::Async(aid, &LeaseServiceActor::LeaseSyncer, getResponse);
        };

        litebus::Async(
            backupActor_, &BackupActor::Watch, META_STORE_BACKUP_LEASE_PREFIX, option,
            [aid(GetAID())](const std::vector<WatchEvent> &events, bool synced) {
                auto respCopy = events;
                litebus::Async(aid, &LeaseServiceActor::HandleLeaseEvents, respCopy, synced);
                return true;
            },
            kvSyncer);
    }
    return true;
}

litebus::Future<SyncResult> LeaseServiceActor::LeaseSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    if (getResponse == nullptr || getResponse->status.IsError()) {
        YRLOG_INFO("failed to get kv from meta storage");
        return SyncResult{ getResponse->status };
    }

    if (getResponse->kvs.empty()) {
        YRLOG_INFO("get no result with lease from meta storage, revision is {}", getResponse->header.revision);
        return SyncResult{ Status::OK() };
    }

    std::vector<WatchEvent> events;
    std::set<std::string> remoteKeys;  // leaseIDs
    for (auto &kv : getResponse->kvs) {
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} };
        events.emplace_back(event);
        remoteKeys.emplace(kv.key().substr(META_STORE_BACKUP_LEASE_PREFIX.size()));
    }

    for (const auto &iter : leases_) {
        if (remoteKeys.find(std::to_string(iter.first)) != remoteKeys.end()) {
            continue;
        }
        YRLOG_WARN("failed to find local lease({}) in etcd, delete", iter.first);
        ::mvccpb::KeyValue kv;
        kv.set_key(META_STORE_BACKUP_LEASE_PREFIX + std::to_string(iter.first));
        kv.set_value(iter.second.SerializeAsString());
        WatchEvent event{ .eventType = EVENT_TYPE_DELETE, .kv = kv, .prevKv = {} };
        events.emplace_back(event);
    }

    HandleLeaseEvents(events, true);
    return SyncResult{ Status::OK() };
}

void LeaseServiceActor::HandleLeaseEvents(const std::vector<WatchEvent> &events, bool synced)
{
    if (curStatus_ == MASTER_BUSINESS && !synced) {
        return;
    }

    for (const auto &event : events) {
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
                ::messages::Lease lease;
                if (!lease.ParseFromString(event.kv.value())) {
                    YRLOG_WARN("failed to parse value for lease({})", event.kv.key());
                    continue;
                }

                if (lease.ttl() * MILLISECONDS_PRE_SECOND > INT64_MAX - milliseconds) {
                    lease.set_expiry(0);
                } else {
                    // update expiry
                    lease.set_expiry(milliseconds + (lease.ttl() * MILLISECONDS_PRE_SECOND));
                }

                YRLOG_DEBUG("receive lease({}) put event, key: {}", lease.id(), event.kv.key());
                leases_[lease.id()] = lease;
                break;
            }

            case EVENT_TYPE_DELETE: {
                int64_t leaseID;
                try {
                    leaseID = std::stoll(event.kv.key().substr(META_STORE_BACKUP_LEASE_PREFIX.size()));
                } catch (std::exception &e) {
                    YRLOG_WARN("failed to get leaseID from key: {}", event.kv.key());
                    break;
                }

                if (leases_.find(leaseID) == leases_.end()) {
                    YRLOG_WARN("receive unknown lease({}) delete event", leaseID);
                    break;
                }

                YRLOG_DEBUG("receive lease({}) delete event", leaseID);
                auto lease = leases_[leaseID];
                leases_.erase(leaseID);
                break;
            }

            default: {
                YRLOG_WARN("unknown event type {}", fmt::underlying(event.eventType));
            }
        }
    }
}

Status LeaseServiceActor::Attach(const std::string &item, int64_t leaseID)
{
    auto iterator = leases_.find(leaseID);
    if (iterator == leases_.end()) {
        return Status(StatusCode::FAILED, "lease not found");
    }

    *iterator->second.mutable_items()->Add() = item;
    return Status::OK();
}

void LeaseServiceActor::ReceiveGrant(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!running_) {
        YRLOG_DEBUG("lease service not ready for ReceiveGrant");
        return;
    }

    YRLOG_DEBUG("receive grant request");
    messages::MetaStoreRequest req;
    req.ParseFromString(msg);
    business_->ReceiveGrant(from, req);
}

void LeaseServiceActor::MasterBusiness::ReceiveGrant(const litebus::AID &from, const messages::MetaStoreRequest &req)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    ::etcdserverpb::LeaseGrantRequest request;
    request.ParseFromString(req.requestmsg());

    ::etcdserverpb::LeaseGrantResponse response;
    actor->LeaseGrant(&request, &response);
    YRLOG_DEBUG("success to grant, lease size:{}", actor->leases_.size());

    messages::MetaStoreResponse res;
    res.set_responseid(req.requestid());
    res.set_responsemsg(response.SerializeAsString());
    actor->Send(from, "GrantCallback", res.SerializeAsString());
}

::grpc::Status LeaseServiceActor::LeaseGrant(const ::etcdserverpb::LeaseGrantRequest *request,
                                             ::etcdserverpb::LeaseGrantResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "null request or response");
    }

    auto header = response->mutable_header();
    header->set_cluster_id(META_STORE_CLUSTER_ID);
    header->set_member_id(META_STORE_MEMBER_ID);
    header->set_revision(META_STORE_REVISION);
    header->set_raft_term(META_STORE_RAFT_TERM);

    if (request->id() == 0) {
        response->set_id(index_++);
    } else {
        response->set_id(request->id());
    }
    response->set_ttl(request->ttl());

    ::messages::Lease lease;
    lease.set_id(response->id());
    lease.set_ttl(response->ttl());

    std::chrono::milliseconds milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    if (response->ttl() * MILLISECONDS_PRE_SECOND > INT64_MAX - milliseconds.count()) {
        lease.set_expiry(0);
        YRLOG_ERROR("ttl({}) is out of range", response->ttl());
        return grpc::Status(grpc::StatusCode::OUT_OF_RANGE,
                            "ttl(" + std::to_string(response->ttl()) + ") is out of range");
    }
    lease.set_expiry(milliseconds.count() + (response->ttl() * MILLISECONDS_PRE_SECOND));
    PutOption putOption;
    litebus::Async(backupActor_, &BackupActor::Put, META_STORE_BACKUP_LEASE_PREFIX + std::to_string(lease.id()),
                   lease.SerializeAsString(), putOption);
    leases_[response->id()] = lease;
    return grpc::Status::OK;
}

void LeaseServiceActor::ReceiveRevoke(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!running_) {
        YRLOG_DEBUG("lease service not ready for ReceiveRevoke");
        return;
    }

    YRLOG_DEBUG("receive revoke request");
    messages::MetaStoreRequest req;
    req.ParseFromString(msg);

    business_->ReceiveRevoke(from, req);
}

void LeaseServiceActor::MasterBusiness::ReceiveRevoke(const litebus::AID &from, const messages::MetaStoreRequest &req)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    ::etcdserverpb::LeaseRevokeRequest request;
    request.ParseFromString(req.requestmsg());

    ::etcdserverpb::LeaseRevokeResponse response;
    actor->LeaseRevoke(&request, &response);
    YRLOG_DEBUG("success to revoke, lease size:{}", actor->leases_.size());

    messages::MetaStoreResponse res;
    res.set_responseid(req.requestid());
    res.set_responsemsg(response.SerializeAsString());
    actor->Send(from, "RevokeCallback", res.SerializeAsString());
}

::grpc::Status LeaseServiceActor::LeaseRevoke(const ::etcdserverpb::LeaseRevokeRequest *request,
                                              ::etcdserverpb::LeaseRevokeResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "null request or response");
    }

    auto header = response->mutable_header();
    header->set_revision(META_STORE_REVISION);
    header->set_raft_term(META_STORE_RAFT_TERM);
    header->set_cluster_id(META_STORE_CLUSTER_ID);
    header->set_member_id(META_STORE_MEMBER_ID);

    auto iterator = leases_.find(request->id());
    if (iterator == leases_.end()) {
        grpc::Status status(grpc::StatusCode::NOT_FOUND, "lease not found");
        return status;
    }

    std::set<std::string> items;
    for (const auto &item : iterator->second.items()) {
        // trans to std::set<std::string>
        items.emplace(item);
    }
    litebus::Async(kvServiceActor_, &KvServiceActor::OnRevoke, items);

    DeleteOption option;
    litebus::Async(backupActor_, &BackupActor::Delete, META_STORE_BACKUP_LEASE_PREFIX + std::to_string(request->id()),
                   option);
    leases_.erase(iterator);

    return grpc::Status::OK;
}

void LeaseServiceActor::ReceiveKeepAlive(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!running_) {
        YRLOG_DEBUG("lease service not ready for ReceiveKeepAlive");
        return;
    }

    YRLOG_DEBUG("receive LeaseKeepAlive request");
    messages::MetaStoreRequest req;
    req.ParseFromString(msg);
    business_->ReceiveKeepAlive(from, req);
}

void LeaseServiceActor::MasterBusiness::ReceiveKeepAlive(const litebus::AID &from,
                                                         const messages::MetaStoreRequest &req)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);

    ::etcdserverpb::LeaseKeepAliveRequest request;
    request.ParseFromString(req.requestmsg());

    ::etcdserverpb::LeaseKeepAliveResponse response;
    actor->LeaseKeepAlive(&request, &response);
    YRLOG_DEBUG("success to KeepAlive, lease size:{}", actor->leases_.size());

    messages::MetaStoreResponse res;
    res.set_responseid(req.requestid());
    res.set_responsemsg(response.SerializeAsString());
    actor->Send(from, "KeepAliveCallback", res.SerializeAsString());
}

::grpc::Status LeaseServiceActor::LeaseKeepAlive(const ::etcdserverpb::LeaseKeepAliveRequest *request,
                                                 ::etcdserverpb::LeaseKeepAliveResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "null request or response");
    }

    auto header = response->mutable_header();
    header->set_cluster_id(META_STORE_CLUSTER_ID);
    header->set_member_id(META_STORE_MEMBER_ID);
    header->set_revision(META_STORE_REVISION);
    header->set_raft_term(META_STORE_RAFT_TERM);

    auto iterator = leases_.find(request->id());
    if (iterator != leases_.end()) {
        response->set_id(iterator->first);
        response->set_ttl(iterator->second.ttl());

        std::chrono::milliseconds milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        if (response->ttl() * MILLISECONDS_PRE_SECOND > INT64_MAX - milliseconds.count()) {
            iterator->second.set_expiry(0);
            YRLOG_ERROR("ttl({}) is out of range", response->ttl());
            return grpc::Status(grpc::StatusCode::OUT_OF_RANGE,
                                "ttl(" + std::to_string(response->ttl()) + ") is out of range");
        }
        iterator->second.set_expiry(milliseconds.count() + (iterator->second.ttl() * MILLISECONDS_PRE_SECOND));
    }

    return grpc::Status::OK;
}

void LeaseServiceActor::CheckpointScheduledLeases()
{
    if (!running_) {
        YRLOG_DEBUG("lease service already closed");
        return;
    }

    if (curStatus_ == SLAVE_BUSINESS) {
        YRLOG_DEBUG("slave lease service, stop check lease");
        return;
    }

    int64_t milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    for (auto iterator = leases_.begin(); iterator != leases_.end();) {
        if (iterator->second.expiry() >= milliseconds) {
            iterator++;
            continue;
        }

        std::set<std::string> items;
        for (const auto &item : iterator->second.items()) {
            // trans to std::set<std::string>
            items.emplace(item);
        }

        YRLOG_INFO("lease({}) expired, expiry: {}, current:{}, ttl: {}, item size: {}", iterator->first,
                   iterator->second.expiry(), milliseconds, iterator->second.ttl(), items.size());
        litebus::Async(kvServiceActor_, &KvServiceActor::OnRevoke, items);
        litebus::Async(backupActor_, &BackupActor::Delete,
                       META_STORE_BACKUP_LEASE_PREFIX + std::to_string(iterator->second.id()), DeleteOption{});
        leases_.erase(iterator++);
    }
    litebus::AsyncAfter(LEASE_WAIT_TIME_MS, GetAID(), &LeaseServiceActor::CheckpointScheduledLeases);
}

void LeaseServiceActor::OnHealthyStatus(const Status &status)
{
    YRLOG_DEBUG("LeaseServiceActor health status changes to healthy({})", status.IsOk());
    healthyStatus_ = status;
}

void LeaseServiceActor::UpdateLeaseExpire()
{
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    for (auto &iter : leases_) {
        if (iter.second.ttl() * MILLISECONDS_PRE_SECOND > INT64_MAX - milliseconds) {
            iter.second.set_expiry(0);
        } else {
            // update expiry
            iter.second.set_expiry(milliseconds + (iter.second.ttl() * MILLISECONDS_PRE_SECOND));
        }
    }
}

void LeaseServiceActor::MasterBusiness::OnChange()
{
    YRLOG_INFO("lease service actor changes to master service, start to check leases");
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->UpdateLeaseExpire();
    actor->CheckpointScheduledLeases();
}

void LeaseServiceActor::SlaveBusiness::OnChange()
{
    YRLOG_INFO("lease service actor changes to slave service");
}

void LeaseServiceActor::SlaveBusiness::ReceiveGrant(const litebus::AID &from, const messages::MetaStoreRequest &req)
{
    YRLOG_WARN("{}|slave service receive grant request, ignore", req.requestid());
}

void LeaseServiceActor::SlaveBusiness::ReceiveRevoke(const litebus::AID &from, const messages::MetaStoreRequest &req)
{
    YRLOG_WARN("{}|slave service receive grant request, ignore", req.requestid());
}

void LeaseServiceActor::SlaveBusiness::ReceiveKeepAlive(const litebus::AID &from, const messages::MetaStoreRequest &req)
{
    YRLOG_WARN("{}|slave service receive grant request, ignore", req.requestid());
}
}  // namespace functionsystem::meta_store
