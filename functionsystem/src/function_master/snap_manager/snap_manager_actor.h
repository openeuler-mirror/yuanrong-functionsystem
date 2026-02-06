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

#ifndef FUNCTION_MASTER_SNAP_MANAGER_SNAP_MANAGER_ACTOR_H
#define FUNCTION_MASTER_SNAP_MANAGER_SNAP_MANAGER_ACTOR_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "actor/actor.hpp"
#include "async/future.hpp"

#include "common/constants/actor_name.h"
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "common/status/status.h"
#include "common/proto/pb/message_pb.h"
#include "meta_store_client/meta_store_client.h"
#include "meta_store_client/meta_store_struct.h"
#include "global_scheduler/global_sched.h"

namespace functionsystem::snap_manager {

using namespace functionsystem::explorer;
using GlobalScheduler = functionsystem::global_scheduler::GlobalSched;

// etcd key prefix for snapshot metadata
const std::string SNAPSHOT_KEY_PREFIX = "/yr/snapshot/";

// Snapshot metadata stored in etcd (protobuf)
using SnapshotMetadata = messages::SnapshotMetadata;

/**
 * Configuration for SnapManagerActor
 */
struct SnapManagerConfig {
    int64_t defaultTTLSeconds{7 * 24 * 3600};      // 默认7天过期
    int64_t maxSnapshotsPerFunction{10};           // 每个函数最多保留快照数
    int64_t cleanupIntervalMs{3600 * 1000};        // 清理间隔（1小时）
};

/**
 * SnapManagerActor manages snapshot metadata in function_master.
 * It supports master/slave mode with MetaStoreClient for persistence.
 */
class SnapManagerActor : public litebus::ActorBase, public std::enable_shared_from_this<SnapManagerActor> {
public:
    SnapManagerActor() = delete;

    /**
     * Constructor
     * @param metaClient MetaStoreClient for etcd operations
     * @param globalScheduler Global scheduler for instance scheduling
     * @param config Configuration for snap manager
     */
    SnapManagerActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                     const std::shared_ptr<GlobalScheduler> &globalScheduler,
                     const SnapManagerConfig &config = SnapManagerConfig{});

    ~SnapManagerActor() override = default;

    /**
     * Update leader info and switch business mode
     * @param leaderInfo Current leader information
     * @return true if update successful
     */
    bool UpdateLeaderInfo(const LeaderInfo &leaderInfo);

    /**
     * Record snapshot metadata (called by local_scheduler via message)
     * @param from Source actor AID
     * @param name Message name
     * @param msg Serialized RecordSnapshotRequest
     */
    void RecordSnapshotMetadata(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Handle snapstart request (called by local_scheduler via message)
     * @param from Source actor AID
     * @param name Message name
     * @param msg Serialized SnapStartCheckpointRequest
     */
    void SnapStartFromCheckpoint(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Query snapshot by ID
     * @param snapshotID Snapshot ID to query
     * @return Future with optional SnapshotMetadata
     */
    litebus::Future<litebus::Option<SnapshotMetadata>> GetSnapshotMetadata(const std::string &snapshotID);

    /**
     * List all snapshots for a function
     * @param functionID Function ID
     * @return Future with list of SnapshotMetadata
     */
    litebus::Future<std::vector<SnapshotMetadata>> ListSnapshotsByFunction(const std::string &functionID);

    /**
     * Delete a snapshot
     * @param snapshotID Snapshot ID to delete
     * @return Future with status
     */
    litebus::Future<Status> DeleteSnapshot(const std::string &snapshotID);

    /**
     * Handle schedule completion for snapshot restoration
     * @param req Restore snapshot request
     * @param from Source actor AID
     * @param scheduleReq Schedule request
     * @param snapshotID Snapshot ID
     * @param status Schedule result status
     */
    void OnScheduleDone(
        const std::shared_ptr<messages::RestoreSnapshotRequest> &req,
        const litebus::AID &from,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::string &snapshotID,
        const litebus::Future<Status> &status);

protected:
    void Init() override;
    void Finalize() override;

private:
    // Internal member struct for shared state
    struct Member {
        std::shared_ptr<MetaStoreClient> client{nullptr};
        SnapManagerConfig config;
        LeaderInfo leaderInfo;

        // Snapshot cache: snapshotID -> metadata
        std::unordered_map<std::string, SnapshotMetadata> snapshotCache;

        // Function to snapshots mapping: functionID -> set of snapshotIDs
        std::unordered_map<std::string, std::unordered_set<std::string>> functionSnapshots;

        // Watcher for etcd
        std::shared_ptr<Watcher> snapshotWatcher{nullptr};

        // Cleanup timer
        litebus::Timer cleanupTimer;

        // Global scheduler for instance scheduling
        std::shared_ptr<GlobalScheduler> globalScheduler{nullptr};
    };

    /**
     * Business policy base class for master/slave pattern
     */
    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<Member> &member, const std::shared_ptr<SnapManagerActor> &actor)
            : member_(member), actor_(actor) {}
        ~Business() override = default;

        virtual void RecordSnapshotMetadata(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void SnapStartFromCheckpoint(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual litebus::Future<Status> DeleteSnapshot(const std::string &snapshotID) = 0;
        virtual void CleanupExpiredSnapshots() = 0;
        virtual void OnScheduleDone(
            const std::shared_ptr<messages::RestoreSnapshotRequest> &req,
            const litebus::AID &from,
            const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
            const std::string &snapshotID,
            const litebus::Future<Status> &status) = 0;

    protected:
        std::shared_ptr<Member> member_;
        std::weak_ptr<SnapManagerActor> actor_;
    };

    /**
     * Master business handles write operations
     */
    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<SnapManagerActor> &actor)
            : Business(member, actor) {}
        ~MasterBusiness() override = default;

        void OnChange() override;

        void RecordSnapshotMetadata(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void SnapStartFromCheckpoint(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        litebus::Future<Status> DeleteSnapshot(const std::string &snapshotID) override;
        void CleanupExpiredSnapshots() override;

    private:
        /**
         * Save snapshot metadata to etcd
         * @param meta Snapshot metadata
         * @return Status
         */
        litebus::Future<Status> SaveMetadataToEtcd(const SnapshotMetadata &meta);

        /**
         * Delete snapshot metadata from etcd
         * @param snapshotID Snapshot ID
         * @return Status
         */
        litebus::Future<Status> DeleteMetadataFromEtcd(const std::string &snapshotID);

        /**
         * Enforce quota: delete oldest snapshots if exceeded
         * @param functionID Function ID
         */
        void EnforceSnapshotQuota(const std::string &functionID);

        /**
         * Build ScheduleRequest from snapshot metadata
         * @param snapshotID Snapshot ID
         * @param meta Snapshot metadata
         * @param req Restore snapshot request (for SnapStartOptions)
         * @return ScheduleRequest for restoring the instance
         */
        std::shared_ptr<messages::ScheduleRequest> BuildScheduleRequestFromSnapshot(
            const std::string &snapshotID,
            const SnapshotMetadata &meta,
            const messages::RestoreSnapshotRequest &req);

        /**
         * Handle schedule completion for snapshot restoration
         * @param req Restore snapshot request
         * @param from Source actor AID
         * @param scheduleReq Schedule request
         * @param snapshotID Snapshot ID
         * @param status Schedule result status
         */
        void OnScheduleDone(
            const std::shared_ptr<messages::RestoreSnapshotRequest> &req,
            const litebus::AID &from,
            const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
            const std::string &snapshotID,
            const litebus::Future<Status> &status);
    };

    /**
     * Slave business forwards requests to master
     */
    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<SnapManagerActor> &actor)
            : Business(member, actor) {}
        ~SlaveBusiness() override = default;

        void OnChange() override;

        void RecordSnapshotMetadata(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void SnapStartFromCheckpoint(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        litebus::Future<Status> DeleteSnapshot(const std::string &snapshotID) override;
        void CleanupExpiredSnapshots() override {}
        void OnScheduleDone(
            const std::shared_ptr<messages::RestoreSnapshotRequest> &req,
            const litebus::AID &from,
            const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
            const std::string &snapshotID,
            const litebus::Future<Status> &status) override {}
    };

    // etcd watch and sync methods
    void GetAndWatchSnapshots();
    void OnSnapshotWatchEvent(const std::vector<WatchEvent> &events, bool synced);
    litebus::Future<SyncResult> OnSnapshotSyncer(const std::shared_ptr<GetResponse> &getResponse);
    void OnSnapshotWatch(const std::shared_ptr<Watcher> &watcher);

    // Helper methods
    SnapshotMetadata ParseSnapshotFromKV(const std::string &key, const std::string &value);
    void UpdateSnapshotCache(const std::string &snapshotID, const SnapshotMetadata &meta);
    void RemoveFromSnapshotCache(const std::string &snapshotID);

    // Periodic cleanup task
    void ScheduleCleanupTask();
    void DoCleanupExpiredSnapshots();

    std::shared_ptr<Member> member_{nullptr};
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::shared_ptr<Business> business_{nullptr};

    friend class SnapManagerActorTest;
};

}  // namespace functionsystem::snap_manager

#endif  // FUNCTION_MASTER_SNAP_MANAGER_SNAP_MANAGER_ACTOR_H
