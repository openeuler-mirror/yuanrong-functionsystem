/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#include "function_proxy/local_scheduler/function_agent_manager/local_snapshot_view.h"

#include <gtest/gtest.h>

namespace functionsystem::local_scheduler {
namespace {

messages::LocalSnapshotMetadata MakeSnapshot(const std::string &snapshotID,
                                             const std::string &instanceID,
                                             uint64_t createdAt,
                                             bool recoveryCandidate)
{
    messages::LocalSnapshotMetadata snapshot;
    snapshot.set_snapshotid(snapshotID);
    snapshot.set_instanceid(instanceID);
    snapshot.set_createdatunixseconds(static_cast<int64_t>(createdAt));
    snapshot.set_localrecoverycandidate(recoveryCandidate);
    snapshot.set_size(4096);
    return snapshot;
}

TEST(LocalSnapshotViewTest, SelectsLatestRecoveryCandidatePerInstance)
{
    LocalSnapshotView view;

    const auto status = view.ReplaceAgentSnapshots("agent-a", {
        MakeSnapshot("old", "sandbox-a", 1, true),
        MakeSnapshot("new", "sandbox-a", 2, true),
        MakeSnapshot("named", "sandbox-a", 9, false),
    });

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    const auto latest = view.LatestAnonymous("sandbox-a");
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->snapshotid(), "new");
}

TEST(LocalSnapshotViewTest, EqualGenerationConflictHasNoWinner)
{
    LocalSnapshotView view;

    const auto status = view.ReplaceAgentSnapshots("agent-a", {
        MakeSnapshot("left", "sandbox-a", 2, true),
        MakeSnapshot("right", "sandbox-a", 2, true),
    });

    EXPECT_EQ(status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_FALSE(view.LatestAnonymous("sandbox-a").has_value());
}

TEST(LocalSnapshotViewTest, AgentRestartReplacesOnlyThatAgentsInventory)
{
    LocalSnapshotView view;
    ASSERT_TRUE(view.ReplaceAgentSnapshots("agent-a", {
        MakeSnapshot("a-old", "sandbox-a", 1, true),
    }).IsOk());
    ASSERT_TRUE(view.ReplaceAgentSnapshots("agent-b", {
        MakeSnapshot("b-current", "sandbox-b", 4, true),
    }).IsOk());

    ASSERT_TRUE(view.ReplaceAgentSnapshots("agent-a", {
        MakeSnapshot("a-new", "sandbox-a", 2, true),
    }).IsOk());

    EXPECT_FALSE(view.Find("a-old").has_value());
    EXPECT_EQ(view.LatestAnonymous("sandbox-a")->snapshotid(), "a-new");
    EXPECT_EQ(view.LatestAnonymous("sandbox-b")->snapshotid(), "b-current");
}

TEST(LocalSnapshotViewTest, RecordCommittedReturnsPreviousLatestForExactCleanup)
{
    LocalSnapshotView view;
    ASSERT_TRUE(view.ReplaceAgentSnapshots("agent-a", {
        MakeSnapshot("old", "sandbox-a", 1, true),
    }).IsOk());

    const auto recorded = view.RecordCommitted(
        "agent-a", MakeSnapshot("new", "sandbox-a", 2, true));

    ASSERT_TRUE(recorded.status.IsOk()) << recorded.status.ToString();
    ASSERT_TRUE(recorded.replaced.has_value());
    EXPECT_EQ(recorded.replaced->snapshotid(), "old");
    EXPECT_EQ(recorded.replacedFunctionAgentID, "agent-a");
    EXPECT_EQ(view.LatestAnonymous("sandbox-a")->snapshotid(), "new");
}

}  // namespace
}  // namespace functionsystem::local_scheduler
