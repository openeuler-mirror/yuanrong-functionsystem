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

#include "snap_manager_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/constants/actor_name.h"
#include "common/proto/pb/posix/common.pb.h"
#include "common/logs/logging.h"
#include "common/utils/generate_message.h"

namespace functionsystem::snap_manager {

using namespace functionsystem::explorer;
using namespace std::placeholders;

SnapManagerActor::SnapManagerActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                                   const std::shared_ptr<GlobalScheduler> &globalScheduler,
                                   const SnapManagerConfig &config)
    : ActorBase(SNAP_MANAGER_ACTOR_NAME)
{
    member_ = std::make_shared<Member>();
    member_->client = metaClient;
    member_->globalScheduler = globalScheduler;
    member_->config = config;
}

bool SnapManagerActor::UpdateLeaderInfo(const LeaderInfo &leaderInfo)
{
    litebus::AID masterAID(SNAP_MANAGER_ACTOR_NAME, leaderInfo.address);
    member_->leaderInfo = leaderInfo;

    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("SnapManagerActor UpdateLeaderInfo new status({}) business don't exist", newStatus);
        return false;
    }
    business_ = businesses_[newStatus];
    ASSERT_IF_NULL(business_);
    business_->OnChange();
    curStatus_ = newStatus;
    YRLOG_INFO("SnapManagerActor switched to {} mode", curStatus_);
    return true;
}

void SnapManagerActor::Init()
{
    YRLOG_INFO("init SnapManagerActor");
    ASSERT_IF_NULL(member_);
    ASSERT_IF_NULL(member_->client);

    // Create master and slave business
    auto masterBusiness = std::make_shared<MasterBusiness>(member_, shared_from_this());
    auto slaveBusiness = std::make_shared<SlaveBusiness>(member_, shared_from_this());

    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);

    // Default to slave mode
    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;

    // Register message handlers
    Receive("RecordSnapshotMetadata", &SnapManagerActor::RecordSnapshotMetadata);
    Receive("SnapStartFromCheckpoint", &SnapManagerActor::SnapStartFromCheckpoint);

    // Register leader change callback
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "SnapManager", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &SnapManagerActor::UpdateLeaderInfo, leaderInfo);
        });

    // Start watching snapshots from etcd
    GetAndWatchSnapshots();

    // Schedule periodic cleanup task
    ScheduleCleanupTask();
}

void SnapManagerActor::Finalize()
{
    YRLOG_INFO("finalize SnapManagerActor");
    if (member_->cleanupTimer.IsValid()) {
        litebus::TimerTools::Cancel(member_->cleanupTimer);
    }
}

void SnapManagerActor::RecordSnapshotMetadata(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->RecordSnapshotMetadata(from, std::move(name), std::move(msg));
}

void SnapManagerActor::SnapStartFromCheckpoint(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->SnapStartFromCheckpoint(from, std::move(name), std::move(msg));
}

litebus::Future<litebus::Option<SnapshotMetadata>> SnapManagerActor::GetSnapshotMetadata(const std::string &snapshotID)
{
    auto it = member_->snapshotCache.find(snapshotID);
    if (it != member_->snapshotCache.end()) {
        return litebus::Option<SnapshotMetadata>(it->second);
    }
    return litebus::Option<SnapshotMetadata>::None();
}

litebus::Future<std::vector<SnapshotMetadata>> SnapManagerActor::ListSnapshotsByFunction(const std::string &functionID)
{
    std::vector<SnapshotMetadata> result;
    auto it = member_->functionSnapshots.find(functionID);
    if (it != member_->functionSnapshots.end()) {
        for (const auto &snapshotID : it->second) {
            auto snapIt = member_->snapshotCache.find(snapshotID);
            if (snapIt != member_->snapshotCache.end()) {
                result.push_back(snapIt->second);
            }
        }
    }
    return result;
}

litebus::Future<Status> SnapManagerActor::DeleteSnapshot(const std::string &snapshotID)
{
    ASSERT_IF_NULL(business_);
    return business_->DeleteSnapshot(snapshotID);
}

void SnapManagerActor::OnScheduleDone(
    const std::shared_ptr<messages::RestoreSnapshotRequest> &req,
    const litebus::AID &from,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const std::string &snapshotID,
    const litebus::Future<Status> &status)
{
    ASSERT_IF_NULL(business_);
    business_->OnScheduleDone(req, from, scheduleReq, snapshotID, status);
}

void SnapManagerActor::GetAndWatchSnapshots()
{
    auto observer = [aid(GetAID())](const std::vector<WatchEvent> &events, bool synced) -> bool {
        litebus::Async(aid, &SnapManagerActor::OnSnapshotWatchEvent, events, synced);
        return true;
    };

    auto syncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &SnapManagerActor::OnSnapshotSyncer, getResponse);
    };

    (void)member_->client
        ->GetAndWatch(SNAPSHOT_KEY_PREFIX, {.prefix = true, .prevKv = true}, observer, syncer)
        .Then([aid(GetAID())](const std::shared_ptr<Watcher> &watcher) -> litebus::Future<Status> {
            litebus::Async(aid, &SnapManagerActor::OnSnapshotWatch, watcher);
            return Status::OK();
        });
}

void SnapManagerActor::OnSnapshotWatchEvent(const std::vector<WatchEvent> &events, bool synced)
{
    for (const auto &event : events) {
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                auto meta = ParseSnapshotFromKV(event.kv.key(), event.kv.value());
                const auto &snapshotID = meta.snapshotinfo().checkpointid();
                if (!snapshotID.empty()) {
                    UpdateSnapshotCache(snapshotID, meta);
                    YRLOG_DEBUG("snapshot {} put event processed", snapshotID);
                }
                break;
            }
            case EVENT_TYPE_DELETE: {
                // Extract snapshotID from key: /yr/snapshot/{snapshotID}
                std::string snapshotID = event.kv.key().substr(SNAPSHOT_KEY_PREFIX.length());
                RemoveFromSnapshotCache(snapshotID);
                YRLOG_DEBUG("snapshot {} delete event processed", snapshotID);
                break;
            }
            default:
                break;
        }
    }
}

litebus::Future<SyncResult> SnapManagerActor::OnSnapshotSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    if (getResponse == nullptr) {
        YRLOG_ERROR("OnSnapshotSyncer: getResponse is null");
        return SyncResult{Status(StatusCode::FAILED, "getResponse is null")};
    }

    YRLOG_INFO("syncing {} snapshots from etcd", getResponse->kvs.size());

    for (const auto &kv : getResponse->kvs) {
        auto meta = ParseSnapshotFromKV(kv.key(), kv.value());
        const auto &snapshotID = meta.snapshotinfo().checkpointid();
        if (!snapshotID.empty()) {
            UpdateSnapshotCache(snapshotID, meta);
        }
    }

    YRLOG_INFO("snapshot sync completed, total {} snapshots in cache", member_->snapshotCache.size());
    return SyncResult{Status::OK()};
}

void SnapManagerActor::OnSnapshotWatch(const std::shared_ptr<Watcher> &watcher)
{
    member_->snapshotWatcher = watcher;
}

namespace {
int64_t GetSnapshotCreateTimeSec(const SnapshotMetadata &meta)
{
    const auto &createTime = meta.snapshotinfo().createtime();
    if (createTime.empty()) {
        return 0;
    }
    try {
        return std::stoll(createTime);
    } catch (const std::exception &) {
        return 0;
    }
}
}  // namespace

SnapshotMetadata SnapManagerActor::ParseSnapshotFromKV(const std::string &key, const std::string &value)
{
    SnapshotMetadata meta;
    if (!meta.ParseFromString(value)) {
        YRLOG_ERROR("failed to parse snapshot metadata from key {}", key);
        return meta;
    }
    if (meta.snapshotinfo().checkpointid().empty() && key.size() > SNAPSHOT_KEY_PREFIX.size()) {
        meta.mutable_snapshotinfo()->set_checkpointid(key.substr(SNAPSHOT_KEY_PREFIX.size()));
    }
    return meta;
}

void SnapManagerActor::UpdateSnapshotCache(const std::string &snapshotID, const SnapshotMetadata &meta)
{
    // Update snapshot cache
    member_->snapshotCache[snapshotID] = meta;

    // Update function to snapshots mapping
    const auto &functionID = meta.instanceinfo().function();
    if (!functionID.empty()) {
        member_->functionSnapshots[functionID].insert(snapshotID);
    }
}

void SnapManagerActor::RemoveFromSnapshotCache(const std::string &snapshotID)
{
    auto it = member_->snapshotCache.find(snapshotID);
    if (it != member_->snapshotCache.end()) {
        // Remove from function mapping
        const auto &functionID = it->second.instanceinfo().function();
        if (!functionID.empty()) {
            auto funcIt = member_->functionSnapshots.find(functionID);
            if (funcIt != member_->functionSnapshots.end()) {
                funcIt->second.erase(snapshotID);
                if (funcIt->second.empty()) {
                    member_->functionSnapshots.erase(funcIt);
                }
            }
        }
        // Remove from cache
        member_->snapshotCache.erase(it);
    }
}

void SnapManagerActor::ScheduleCleanupTask()
{
    member_->cleanupTimer = litebus::AsyncAfter(
        member_->config.cleanupIntervalMs,
        GetAID(),
        &SnapManagerActor::DoCleanupExpiredSnapshots);
}

void SnapManagerActor::DoCleanupExpiredSnapshots()
{
    ASSERT_IF_NULL(business_);
    business_->CleanupExpiredSnapshots();

    // Reschedule next cleanup
    ScheduleCleanupTask();
}

// ============================================================================
// MasterBusiness Implementation
// ============================================================================

void SnapManagerActor::MasterBusiness::OnChange()
{
    YRLOG_INFO("SnapManagerActor switched to MASTER mode");
}

void SnapManagerActor::MasterBusiness::RecordSnapshotMetadata(const litebus::AID &from,
                                                              std::string &&name,
                                                              std::string &&msg)
{
    messages::RecordSnapshotRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse RecordSnapshotRequest");
        messages::RecordSnapshotResponse rsp;
        rsp.set_code(common::ERR_PARAM_INVALID);
        rsp.set_message("failed to parse request");
        if (auto actor = actor_.lock(); actor) {
            actor->Send(from, "RecordSnapshotMetadataResponse", rsp.SerializeAsString());
        }
        return;
    }

    // Build snapshot metadata
    SnapshotMetadata meta;
    *meta.mutable_snapshotinfo() = std::move(*req.mutable_snapshotinfo());
    meta.set_ttlseconds(static_cast<int32_t>(member_->config.defaultTTLSeconds));
    *meta.mutable_instanceinfo() = std::move(*req.mutable_instanceinfo());

    // Validate required fields
    if (meta.snapshotinfo().checkpointid().empty()) {
        YRLOG_ERROR("RecordSnapshotMetadata: snapshotID is empty");
        messages::RecordSnapshotResponse rsp;
        rsp.set_code(common::ERR_PARAM_INVALID);
        rsp.set_message("snapshotID is required");
        if (auto actor = actor_.lock(); actor) {
            actor->Send(from, "RecordSnapshotMetadataResponse", rsp.SerializeAsString());
        }
        return;
    }

    YRLOG_INFO("recording snapshot metadata: snapshotID={}, storage={}, size={}",
               meta.snapshotinfo().checkpointid(), meta.snapshotinfo().storage(), meta.size());

    // Save to etcd
    SaveMetadataToEtcd(meta)
        .Then([from, meta, weakActor = actor_](const Status &status) {
            messages::RecordSnapshotResponse rsp;
            if (status.IsOk()) {
                rsp.set_code(common::ERR_NONE);
                rsp.set_message("success");
                YRLOG_INFO("snapshot metadata recorded successfully: {}", meta.snapshotinfo().checkpointid());
            } else {
                rsp.set_code(common::ERR_ETCD_OPERATION_ERROR);
                rsp.set_message(status.GetMessage());
                YRLOG_ERROR("failed to record snapshot metadata: {}, error: {}",
                           meta.snapshotinfo().checkpointid(), status.GetMessage());
            }
            auto actor = weakActor.lock();
            if (actor) {
                actor->Send(from, "RecordSnapshotMetadataResponse", rsp.SerializeAsString());
            }
        });

    // // Enforce quota after recording
    // if (!meta.instanceinfo().function().empty()) {
    //     EnforceSnapshotQuota(meta.instanceinfo().function());
    // }
}

void SnapManagerActor::MasterBusiness::SnapStartFromCheckpoint(const litebus::AID &from,
                                                                std::string &&name,
                                                                std::string &&msg)
{
    auto req = std::make_shared<messages::RestoreSnapshotRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse SnapStartCheckpointRequest");
        messages::RestoreSnapshotResponse rsp;
        rsp.set_code(common::ERR_PARAM_INVALID);
        rsp.set_message("failed to parse request");
        auto actor = actor_.lock();
        if (actor) {
            actor->Send(from, "SnapStartCheckpointResponse", rsp.SerializeAsString());
        }
        return;
    }

    const auto &snapshotID = req->snapshotid();
    YRLOG_INFO("processing snapstart request for snapshot: {}", snapshotID);

    // Look up snapshot metadata from cache
    auto it = member_->snapshotCache.find(snapshotID);
    if (it == member_->snapshotCache.end()) {
        YRLOG_ERROR("snapshot not found: {}", snapshotID);
        messages::RestoreSnapshotResponse rsp;
        rsp.set_code(common::ERR_INSTANCE_NOT_FOUND);  // Snapshot not found
        rsp.set_message("snapshot not found");
        auto actor = actor_.lock();
        if (actor) {
            actor->Send(from, "SnapStartCheckpointResponse", rsp.SerializeAsString());
        }
        return;
    }

    const auto &meta = it->second;
    // Check if snapshot has expired
    int64_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t createTime = GetSnapshotCreateTimeSec(meta);
    if (meta.ttlseconds() > 0 && createTime > 0 && (currentTime - createTime) > meta.ttlseconds()) {
        YRLOG_ERROR("snapshot {} has expired", snapshotID);
        messages::RestoreSnapshotResponse rsp;
        rsp.set_code(common::ERR_PARAM_INVALID);  // Snapshot expired
        rsp.set_message("snapshot has expired");
        auto actor = actor_.lock();
        if (actor) {
            actor->Send(from, "SnapStartCheckpointResponse", rsp.SerializeAsString());
        }
        return;
    }

    // Build ScheduleRequest from snapshot metadata
    auto scheduleReq = BuildScheduleRequestFromSnapshot(snapshotID, meta, *req);

    // Invoke global scheduler to schedule the restored instance
    ASSERT_IF_NULL(member_->globalScheduler);
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor)
    member_->globalScheduler->Schedule(scheduleReq).OnComplete(
        litebus::Defer(actor->GetAID(), &SnapManagerActor::OnScheduleDone, req, from, scheduleReq, snapshotID));
}

void SnapManagerActor::MasterBusiness::OnScheduleDone(
    const std::shared_ptr<messages::RestoreSnapshotRequest> &req,
    const litebus::AID &from,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const std::string &snapshotID,
    const litebus::Future<Status> &status)
{
    messages::RestoreSnapshotResponse rsp;
    rsp.set_requestid(req->requestid());

    if (status.IsError() || status.Get().IsError()) {
        YRLOG_ERROR("failed to schedule restored instance from snapshot {}: code={}, msg={}",
                   snapshotID,
                   status.IsError() ? status.GetErrorCode() : status.Get().StatusCode(),
                   status.IsError() ? "Schedule future error" : status.Get().GetMessage());
        rsp.set_code(common::ERR_INNER_SYSTEM_ERROR);
        rsp.set_message(status.IsError() ? "failed to schedule" : status.Get().GetMessage());
    } else {
        YRLOG_INFO("successfully scheduled restored instance from snapshot {}, requestID/instanceID={}",
                  snapshotID, req->requestid());
        rsp.set_code(common::ERR_NONE);
        rsp.set_message("snapshot restored and scheduled successfully");
        rsp.set_instanceid(scheduleReq->instance().instanceid());
    }

    auto actor = actor_.lock();
    if (actor) {
        actor->Send(from, "SnapStartCheckpointResponse", rsp.SerializeAsString());
    }
}

litebus::Future<Status> SnapManagerActor::MasterBusiness::DeleteSnapshot(const std::string &snapshotID)
{
    YRLOG_INFO("deleting snapshot: {}", snapshotID);
    // todo(lwy): delete snapshot data from storage if needed
    return DeleteMetadataFromEtcd(snapshotID);
}

void SnapManagerActor::MasterBusiness::CleanupExpiredSnapshots()
{
    YRLOG_INFO("starting cleanup of expired snapshots");

    int64_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<std::string> expiredSnapshots;

    for (const auto &[snapshotID, meta] : member_->snapshotCache) {
        const int64_t createTime = GetSnapshotCreateTimeSec(meta);
        if (meta.ttlseconds() > 0 && createTime > 0 && (currentTime - createTime) > meta.ttlseconds()) {
            expiredSnapshots.push_back(snapshotID);
        }
    }

    YRLOG_INFO("found {} expired snapshots to delete", expiredSnapshots.size());

    for (const auto &snapshotID : expiredSnapshots) {
        DeleteMetadataFromEtcd(snapshotID)
            .Then([snapshotID](const Status &status) {
                if (status.IsOk()) {
                    YRLOG_INFO("expired snapshot {} deleted", snapshotID);
                } else {
                    YRLOG_ERROR("failed to delete expired snapshot {}: {}",
                               snapshotID, status.GetMessage());
                }
            });
    }
}

litebus::Future<Status> SnapManagerActor::MasterBusiness::SaveMetadataToEtcd(const SnapshotMetadata &meta)
{
    ASSERT_IF_NULL(member_->client);

    std::string key = SNAPSHOT_KEY_PREFIX + meta.snapshotinfo().checkpointid();
    std::string value = meta.SerializeAsString();
    return member_->client->Put(key, value)
        .Then([](const std::shared_ptr<PutResponse> &response) -> Status {
            if (response && response->status.IsOk()) {
                return Status::OK();
            }
            return Status(StatusCode::ERR_ETCD_OPERATION_ERROR, "failed to put snapshot metadata");
        });
}

litebus::Future<Status> SnapManagerActor::MasterBusiness::DeleteMetadataFromEtcd(const std::string &snapshotID)
{
    ASSERT_IF_NULL(member_->client);

    std::string key = SNAPSHOT_KEY_PREFIX + snapshotID;
    return member_->client->Delete(key)
        .Then([](const std::shared_ptr<DeleteResponse> &response) -> Status {
            if (response && response->status.IsOk()) {
                return Status::OK();
            }
            return Status(StatusCode::ERR_ETCD_OPERATION_ERROR, "failed to delete snapshot metadata");
        });
}

void SnapManagerActor::MasterBusiness::EnforceSnapshotQuota(const std::string &functionID)
{
    auto it = member_->functionSnapshots.find(functionID);
    if (it == member_->functionSnapshots.end()) {
        return;
    }

    const auto &snapshotIDs = it->second;
    if (static_cast<int64_t>(snapshotIDs.size()) <= member_->config.maxSnapshotsPerFunction) {
        return;
    }

    // Build list with create times for sorting
    std::vector<std::pair<int64_t, std::string>> snapshotsWithTime;
    for (const auto &snapshotID : snapshotIDs) {
        auto snapIt = member_->snapshotCache.find(snapshotID);
        if (snapIt != member_->snapshotCache.end()) {
            snapshotsWithTime.emplace_back(GetSnapshotCreateTimeSec(snapIt->second), snapshotID);
        }
    }

    // Sort by create time (oldest first)
    std::sort(snapshotsWithTime.begin(), snapshotsWithTime.end());

    // Delete oldest snapshots to meet quota
    int64_t toDelete = static_cast<int64_t>(snapshotsWithTime.size()) - member_->config.maxSnapshotsPerFunction;
    for (int64_t i = 0; i < toDelete && i < static_cast<int64_t>(snapshotsWithTime.size()); ++i) {
        const auto &snapshotID = snapshotsWithTime[i].second;
        YRLOG_INFO("enforcing quota: deleting old snapshot {} for function {}",
                   snapshotID, functionID);
        DeleteMetadataFromEtcd(snapshotID);
    }
}

std::shared_ptr<messages::ScheduleRequest> SnapManagerActor::MasterBusiness::BuildScheduleRequestFromSnapshot(
    const std::string &snapshotID,
    const SnapshotMetadata &meta,
    const messages::RestoreSnapshotRequest &req)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();

    // Generate requestID and instanceID based on snapshotID (max 64 chars)
    // Format: "{shortened_snapshotID}-{8char_uuid}"
    // requestID and instanceID are kept the same
    std::string snapshotIDShort = snapshotID;
    if (snapshotID.length() > 50) {
        // Take first 20 and last 20 chars to stay within 64 char limit
        snapshotIDShort = snapshotID.substr(0, 20) + "-" + snapshotID.substr(snapshotID.length() - 20);
    }
    std::string newID = snapshotIDShort + "-" +
        litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8);

    scheduleReq->set_requestid(newID);
    scheduleReq->set_traceid(newID);  // Use the same ID for traceability

    // Copy instance info from snapshot metadata
    scheduleReq->mutable_instance()->CopyFrom(meta.instanceinfo());

    // Reset fields that must be regenerated for the new instance
    // Keep requestID and instanceID the same
    scheduleReq->mutable_instance()->set_instanceid(newID);
    scheduleReq->mutable_instance()->set_requestid(newID);
    scheduleReq->mutable_instance()->set_functionproxyid("");  // Will be assigned by scheduler
    scheduleReq->mutable_instance()->set_functionagentid("");  // Will be assigned by scheduler
    scheduleReq->mutable_instance()->set_runtimeid("");        // Will be assigned by runtime
    scheduleReq->mutable_instance()->set_runtimeaddress("");   // Will be assigned by runtime
    scheduleReq->mutable_instance()->set_parentid("InstanceManagerOwner");  // Set to InstanceManager
    scheduleReq->mutable_instance()->clear_args();  // Clear args for restoration
    scheduleReq->mutable_instance()->mutable_snapinfo()->CopyFrom(meta.snapshotinfo());
    // Set instance state to NEW for restoration
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::NEW));

    // Add snapshot restore information to create options
    auto createOptions = scheduleReq->mutable_instance()->mutable_createoptions();
    (*createOptions)["snapshot_id"] = snapshotID;
    (*createOptions)["snapshot_storage"] = meta.snapshotinfo().storage();

    // If SnapStartOptions are provided, serialize and add to create options
    if (req.has_snapstartoptions()) {
        (*createOptions)["snapstart_options"] = req.snapstartoptions().SerializeAsString();
    }

    YRLOG_DEBUG("built ScheduleRequest from snapshot {}: requestID/instanceID={}, traceID={}",
                snapshotID, newID, scheduleReq->traceid());

    return scheduleReq;
}

// ============================================================================
// SlaveBusiness Implementation
// ============================================================================

void SnapManagerActor::SlaveBusiness::OnChange()
{
    YRLOG_INFO("SnapManagerActor switched to SLAVE mode");
}

void SnapManagerActor::SlaveBusiness::RecordSnapshotMetadata(const litebus::AID &from,
                                                             std::string &&name,
                                                             std::string &&msg)
{
    // Forward to master
    auto actor = actor_.lock();
    if (!actor) {
        YRLOG_ERROR("SlaveBusiness: actor is null");
        return;
    }

    litebus::AID masterAID(SNAP_MANAGER_ACTOR_NAME, member_->leaderInfo.address);
    YRLOG_DEBUG("forwarding RecordSnapshotMetadata to master: {}", std::string(masterAID));
    actor->Send(masterAID, "RecordSnapshotMetadata", std::move(msg));
}

void SnapManagerActor::SlaveBusiness::SnapStartFromCheckpoint(const litebus::AID &from,
                                                               std::string &&name,
                                                               std::string &&msg)
{
    // Forward to master
    auto actor = actor_.lock();
    if (!actor) {
        YRLOG_ERROR("SlaveBusiness: actor is null");
        return;
    }

    litebus::AID masterAID(SNAP_MANAGER_ACTOR_NAME, member_->leaderInfo.address);
    YRLOG_DEBUG("forwarding SnapStartFromCheckpoint to master: {}", std::string(masterAID));
    actor->Send(masterAID, "SnapStartFromCheckpoint", std::move(msg));
}

litebus::Future<Status> SnapManagerActor::SlaveBusiness::DeleteSnapshot(const std::string &snapshotID)
{
    // Slave cannot delete, return error
    YRLOG_WARN("SlaveBusiness: DeleteSnapshot called on slave, operation not allowed");
    return Status(StatusCode::FAILED, "operation not allowed on slave");
}

}  // namespace functionsystem::snap_manager
