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

#include "snapshot_scheduler.h"

#include "async/uuid_generator.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/posix/common.pb.h"
#include "common/types/instance_state.h"

namespace functionsystem::snap_manager {

std::shared_ptr<messages::ScheduleRequest> SnapshotScheduler::BuildScheduleRequest(
    const SnapshotMetadata &meta,
    const messages::RestoreSnapshotRequest &restoreReq) const
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();

    const auto &snapshotID = meta.snapshotinfo().checkpointid();
    std::string newID = GenerateInstanceID(snapshotID);

    scheduleReq->set_requestid(newID);
    scheduleReq->set_traceid(newID);

    // Copy instance info from snapshot metadata
    scheduleReq->mutable_instance()->CopyFrom(meta.instanceinfo());

    // Reset fields that must be regenerated for the new instance
    scheduleReq->mutable_instance()->set_instanceid(newID);
    scheduleReq->mutable_instance()->set_requestid(newID);
    scheduleReq->mutable_instance()->set_functionproxyid("");
    scheduleReq->mutable_instance()->set_functionagentid("");
    scheduleReq->mutable_instance()->set_runtimeid("");
    scheduleReq->mutable_instance()->set_runtimeaddress("");
    // todo(lwy): parentID should be passed from restore request
    // scheduleReq->mutable_instance()->set_parentid("InstanceManagerOwner");
    scheduleReq->mutable_instance()->clear_args();
    scheduleReq->mutable_instance()->set_version(0);
    scheduleReq->mutable_instance()->mutable_snapshotinfo()->CopyFrom(meta.snapshotinfo());

    // Set instance state to NEW for restoration
    scheduleReq->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::NEW));

    // TODO: If SnapStartOptions are provided, serialize and add to create options
    YRLOG_DEBUG("built ScheduleRequest from snapshot {}: instanceID={}", snapshotID, newID);
    return scheduleReq;
}

litebus::Future<Status> SnapshotScheduler::Schedule(
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq)
{
    if (!globalScheduler_) {
        YRLOG_ERROR("globalScheduler is null");
        return Status(StatusCode::FAILED, "globalScheduler is null");
    }
    return globalScheduler_->Schedule(scheduleReq);
}

messages::RestoreSnapshotResponse SnapshotScheduler::BuildRestoreResponse(
    const RestoreContext &context,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const Status &status)
{
    messages::RestoreSnapshotResponse rsp;
    rsp.set_requestid(context.request->requestid());

    if (status.IsOk()) {
        rsp.set_code(common::ERR_NONE);
        rsp.set_message("snapshot restored and scheduled successfully");
        if (scheduleReq) {
            rsp.set_instanceid(scheduleReq->instance().instanceid());
        }
        YRLOG_INFO("successfully scheduled restored instance from snapshot {}, instanceID={}",
                  context.snapshotID, rsp.instanceid());
    } else {
        rsp.set_code(common::ERR_INNER_SYSTEM_ERROR);
        rsp.set_message(status.GetMessage());
        YRLOG_ERROR("failed to schedule restored instance from snapshot {}: {}",
                   context.snapshotID, status.GetMessage());
    }

    return rsp;
}

std::string SnapshotScheduler::GenerateInstanceID(const std::string &snapshotID)
{
    // Format: "{shortened_snapshotID}-{8char_uuid}"
    std::string snapshotIDShort = snapshotID;
    if (snapshotID.length() > 50) {
        // Take first 13 and last 8 chars
        snapshotIDShort = snapshotID.substr(0, 13) + "-" + snapshotID.substr(snapshotID.length() - 8);
    }
    return snapshotIDShort + "-" +
           litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8);
}

}  // namespace functionsystem::snap_manager
