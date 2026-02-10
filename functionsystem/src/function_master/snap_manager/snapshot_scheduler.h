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

#ifndef FUNCTION_MASTER_SNAP_MANAGER_SNAPSHOT_SCHEDULER_H
#define FUNCTION_MASTER_SNAP_MANAGER_SNAPSHOT_SCHEDULER_H

#include <string>
#include <memory>

#include "async/future.hpp"
#include "common/status/status.h"
#include "common/proto/pb/posix/message.pb.h"
#include "function_master/global_scheduler/global_sched.h"

namespace functionsystem::snap_manager {

using SnapshotMetadata = ::messages::SnapshotMetadata;
using GlobalScheduler = functionsystem::global_scheduler::GlobalSched;

/**
 * Context for snapshot restoration scheduling
 */
struct RestoreContext {
    std::string snapshotID;
    std::shared_ptr<messages::RestoreSnapshotRequest> request;
    litebus::AID requester;

    RestoreContext(const std::string &id,
                  std::shared_ptr<messages::RestoreSnapshotRequest> req,
                  const litebus::AID &from)
        : snapshotID(id), request(std::move(req)), requester(from) {}
};

/**
 * SnapshotScheduler handles snapshot restoration scheduling logic.
 * Builds schedule requests and manages scheduling callbacks.
 */
class SnapshotScheduler {
public:
    explicit SnapshotScheduler(std::shared_ptr<GlobalScheduler> scheduler)
        : globalScheduler_(std::move(scheduler)) {}

    ~SnapshotScheduler() = default;

    // Disable copy
    SnapshotScheduler(const SnapshotScheduler &) = delete;
    SnapshotScheduler &operator=(const SnapshotScheduler &) = delete;

    /**
     * Build ScheduleRequest from snapshot metadata
     * @param meta Snapshot metadata
     * @param restoreReq Restore request (for SnapStartOptions)
     * @return ScheduleRequest for restoring the instance
     */
    std::shared_ptr<messages::ScheduleRequest> BuildScheduleRequest(
        const SnapshotMetadata &meta,
        const messages::RestoreSnapshotRequest &restoreReq) const;

    /**
     * Schedule snapshot restoration
     * @param scheduleReq Schedule request
     * @return Future with scheduling status
     */
    litebus::Future<Status> Schedule(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    /**
     * Build restore response
     * @param context Restore context
     * @param scheduleReq Schedule request (contains instanceID)
     * @param status Schedule result status
     * @return RestoreSnapshotResponse
     */
    static messages::RestoreSnapshotResponse BuildRestoreResponse(
        const RestoreContext &context,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const Status &status);

private:
    std::shared_ptr<GlobalScheduler> globalScheduler_;

    // Helper: Generate unique ID for restored instance
    static std::string GenerateInstanceID(const std::string &snapshotID);
};

}  // namespace functionsystem::snap_manager

#endif  // FUNCTION_MASTER_SNAP_MANAGER_SNAPSHOT_SCHEDULER_H
