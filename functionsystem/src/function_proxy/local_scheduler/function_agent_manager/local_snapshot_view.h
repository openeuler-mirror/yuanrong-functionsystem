/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef FUNCTIONSYSTEM_LOCAL_SCHEDULER_LOCAL_SNAPSHOT_VIEW_H
#define FUNCTIONSYSTEM_LOCAL_SCHEDULER_LOCAL_SNAPSHOT_VIEW_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::local_scheduler {

struct LocalSnapshotRecordResult {
    Status status;
    std::optional<messages::LocalSnapshotMetadata> replaced;
    std::string replacedFunctionAgentID;
};

class LocalSnapshotView {
public:
    Status ReplaceAgentSnapshots(
        const std::string &functionAgentID,
        const std::vector<messages::LocalSnapshotMetadata> &snapshots);
    LocalSnapshotRecordResult RecordCommitted(
        const std::string &functionAgentID,
        const messages::LocalSnapshotMetadata &snapshot);
    std::optional<messages::LocalSnapshotMetadata> LatestAnonymous(
        const std::string &instanceID) const;
    std::optional<messages::LocalSnapshotMetadata> Find(
        const std::string &snapshotID) const;
    void Remove(const std::string &snapshotID);

private:
    struct Entry {
        std::string functionAgentID;
        messages::LocalSnapshotMetadata metadata;
    };

    static Status Validate(const messages::LocalSnapshotMetadata &snapshot);
    Status RebuildLatest();

    std::unordered_map<std::string, Entry> snapshots_;
    std::unordered_map<std::string, std::string> latestAnonymous_;
};

}  // namespace functionsystem::local_scheduler

#endif  // FUNCTIONSYSTEM_LOCAL_SCHEDULER_LOCAL_SNAPSHOT_VIEW_H
