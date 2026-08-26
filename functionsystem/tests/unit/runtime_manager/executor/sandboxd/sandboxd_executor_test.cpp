/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "runtime_manager/executor/sandboxd/sandboxd_executor.h"
#include "common/constants/constants.h"
#include "common/status/status.h"
#include "runtime_manager/port/port_manager.h"
#include "common/utils/resume_identity.h"
#include "runtime_manager/ckpt/checkpoint_plan.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

runtime::v1::SandboxStatus RunningSandbox(
    const std::string &runtimeID, const std::vector<std::string> &ports,
    const std::string &instanceID = "instance", const std::string &requestID = "request",
    const std::string &sandboxID = "")
{
    runtime::v1::SandboxStatus sandbox;
    sandbox.set_id(sandboxID.empty() ? "sandboxd-generated-" + runtimeID : sandboxID);
    sandbox.set_state(runtime::v1::SANDBOX_STATE_RUNNING);
    (*sandbox.mutable_labels())["instance_id"] = instanceID;
    (*sandbox.mutable_labels())["request_id"] = requestID;
    (*sandbox.mutable_labels())["runtime_id"] = runtimeID;
    for (const auto &port : ports) {
        sandbox.add_ports(port);
    }
    return sandbox;
}

// ── SandboxdExecutor static helpers ───────────────────────────────────────────

TEST(SandboxdExecutorTest, ParseForwardPortsParsesPortForwardings)
{
    const std::string netJson = R"({"portForwardings":[{"port":8080},{"port":9090,"protocol":"TCP"}]})";
    auto configs = SandboxdExecutor::ParseForwardPorts(netJson);
    ASSERT_EQ(configs.size(), 2u);
    EXPECT_EQ(configs[0].containerPort, 8080u);
    EXPECT_EQ(configs[0].protocol, "tcp");
    EXPECT_EQ(configs[1].containerPort, 9090u);
    EXPECT_EQ(configs[1].protocol, "tcp");  // lowercased
}

TEST(SandboxdExecutorTest, ParseForwardPortsParsesRouteKinds)
{
    const std::string netJson = R"({"portForwardings":[)"
        R"({"port":50090,"protocol":"http","routeKind":"direct"},)"
        R"({"port":8765,"protocol":"http","routeKind":"tunnel"},)"
        R"({"port":8080,"protocol":"http"}]})";
    auto configs = SandboxdExecutor::ParseForwardPorts(netJson);
    ASSERT_EQ(configs.size(), 3);
    EXPECT_EQ(configs[0].routeKind, PortRouteKind::DIRECT);
    EXPECT_EQ(configs[1].routeKind, PortRouteKind::TUNNEL);
    EXPECT_EQ(configs[2].routeKind, PortRouteKind::PUBLIC);
}

TEST(SandboxdExecutorTest, ParseForwardPortsInvalidRouteKindIsSkipped)
{
    const std::string netJson = R"({"portForwardings":[)"
        R"({"port":8080,"routeKind":"unknown"},)"
        R"({"port":8081,"routeKind":42},)"
        R"({"port":9090,"routeKind":"public"}]})";
    const auto configs = SandboxdExecutor::ParseForwardPorts(netJson);
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].containerPort, 9090u);
    EXPECT_EQ(configs[0].routeKind, PortRouteKind::PUBLIC);
}

TEST(SandboxdExecutorTest, ParseForwardPortsEmptyOrInvalidReturnsEmpty)
{
    EXPECT_TRUE(SandboxdExecutor::ParseForwardPorts("").empty());
    EXPECT_TRUE(SandboxdExecutor::ParseForwardPorts("not-json").empty());
    EXPECT_TRUE(SandboxdExecutor::ParseForwardPorts(R"({"portForwardings":[]})").empty());
    // Out-of-range ports are skipped.
    auto configs = SandboxdExecutor::ParseForwardPorts(R"({"portForwardings":[{"port":0},{"port":70000}]})");
    EXPECT_TRUE(configs.empty());
}

TEST(SandboxdExecutorTest, IsRetryableWaitErrorClassifiesTransportErrors)
{
    EXPECT_TRUE(SandboxdExecutor::IsRetryableWaitError(Status(GRPC_UNAVAILABLE)));
    EXPECT_TRUE(SandboxdExecutor::IsRetryableWaitError(Status(GRPC_DEADLINE_EXCEEDED)));
    EXPECT_FALSE(SandboxdExecutor::IsRetryableWaitError(Status(GRPC_NOT_FOUND)));
    EXPECT_FALSE(SandboxdExecutor::IsRetryableWaitError(Status(SUCCESS)));
}

TEST(SandboxdExecutorTest, SandboxReclaimBackoffIsExponentialAndCapped)
{
    EXPECT_EQ(SandboxdExecutor::SandboxReclaimBackoffMs(1), 1000u);
    EXPECT_EQ(SandboxdExecutor::SandboxReclaimBackoffMs(2), 2000u);
    EXPECT_EQ(SandboxdExecutor::SandboxReclaimBackoffMs(6), 32000u);
    EXPECT_EQ(SandboxdExecutor::SandboxReclaimBackoffMs(7), 60000u);
    EXPECT_EQ(SandboxdExecutor::SandboxReclaimBackoffMs(100), 60000u);
}

TEST(SandboxdExecutorTest, FailedLocalReclaimKeepsRecoveredPortsUntilDeleteSucceeds)
{
    struct PortManagerReset {
        ~PortManagerReset()
        {
            PortManager::GetInstance().Clear();
        }
    } reset;

    int firstPort = -1;
    for (int candidate = 20000; candidate <= 65534; ++candidate) {
        if (!PortManager::GetInstance().CheckPortInUse(candidate) &&
            !PortManager::GetInstance().CheckPortInUse(candidate + 1)) {
            firstPort = candidate;
            break;
        }
    }
    ASSERT_GT(firstPort, 0);

    const std::string runtimeID = "recovered-runtime";
    const std::string sandboxID = "recovered-sandbox";
    auto &portManager = PortManager::GetInstance();
    portManager.InitPortResource(firstPort, 2);
    portManager.BeginReconcile();
    ASSERT_TRUE(portManager.RebuildPorts({{runtimeID, {firstPort, firstPort + 1}}}));

    auto healthCheck = std::make_shared<HealthCheck>("recovered-port-release-health-check");
    SandboxdExecutor executor("recovered-port-release-executor", litebus::AID(),
                              "/tmp/recovered-port-release-checkpoints");
    executor.SetHealthCheckClient(healthCheck);

    messages::RuntimeInstanceInfo instanceInfo;
    instanceInfo.set_instanceid("recovered-instance");
    instanceInfo.set_runtimeid(runtimeID);
    const std::string portMappings = "[\"public+http:" + std::to_string(firstPort) +
                                     ":8080\",\"public+http:" + std::to_string(firstPort + 1) + ":8081\"]";
    executor.stateManager_.Register({runtimeID, sandboxID, {}, portMappings, instanceInfo});

    (void)executor.CleanupSandboxAfterMaxRetries(runtimeID, sandboxID);

    ASSERT_TRUE(executor.stateManager_.IsActive(runtimeID));
    EXPECT_EQ(executor.stateManager_.GetSandboxID(runtimeID), sandboxID);
    ASSERT_EQ(executor.sandboxLifecycleStates_.count(runtimeID), 1u);
    EXPECT_EQ(executor.sandboxLifecycleStates_.at(runtimeID), SandboxdExecutor::SandboxLifecycleStatus::ABNORMAL);
    ASSERT_EQ(executor.sandboxReclaims_.count(runtimeID), 1u);
    EXPECT_TRUE(portManager.RequestPorts("other-runtime", 1).empty());

    auto failedDelete = executor.OnReclaimSandboxDone(
        runtimeID, sandboxID, "delete-request",
        litebus::Try<runtime::v1::DeleteResponse>(static_cast<int32_t>(GRPC_UNAVAILABLE)));
    ASSERT_TRUE(failedDelete.Get().IsError());
    ASSERT_EQ(executor.sandboxReclaims_.count(runtimeID), 1u);
    EXPECT_EQ(executor.sandboxReclaims_.at(runtimeID).failedAttempts, 1u);
    EXPECT_TRUE(executor.stateManager_.IsActive(runtimeID));
    EXPECT_TRUE(portManager.RequestPorts("other-runtime", 1).empty());

    auto successfulDelete = executor.OnReclaimSandboxDone(
        runtimeID, sandboxID, "delete-request",
        litebus::Try<runtime::v1::DeleteResponse>(runtime::v1::DeleteResponse{}));
    ASSERT_TRUE(successfulDelete.Get().IsOk());
    EXPECT_FALSE(executor.stateManager_.IsActive(runtimeID));
    EXPECT_EQ(executor.sandboxLifecycleStates_.count(runtimeID), 0u);
    EXPECT_EQ(executor.sandboxReclaims_.count(runtimeID), 0u);
    EXPECT_TRUE(portManager.GetPort(runtimeID).empty());
    EXPECT_EQ(portManager.RequestPorts("next-runtime", 2), (std::vector<int>{firstPort, firstPort + 1}));
}

TEST(SandboxdExecutorTest, StartupReconciliationUsesRuntimeOwnerAndIgnoresSandboxIdentity)
{
    runtime::v1::ListSandboxesResponse listed;
    auto first = RunningSandbox("runtime-a", {"tcp:21006:50090"}, {}, {},
                                "sandboxd-generated-01JY7A");
    first.mutable_labels()->erase("instance_id");
    first.mutable_labels()->erase("request_id");
    *listed.add_sandboxes() = first;
    auto second = RunningSandbox("runtime-b", {"udp:21007:8765"}, {}, {},
                                 "unrelated-physical-handle");
    second.mutable_labels()->erase("instance_id");
    second.mutable_labels()->erase("request_id");
    *listed.add_sandboxes() = second;

    PortManager::ReservationMap reservations;
    const auto status =
        SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(listed, reservations);

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_EQ(reservations.at("runtime-a"), std::vector<int>({21006}));
    EXPECT_EQ(reservations.at("runtime-b"), std::vector<int>({21007}));
}

TEST(SandboxdExecutorTest, StartupReconciliationDoesNotReadOrDeriveSandboxIdentity)
{
    runtime::v1::ListSandboxesResponse listed;
    auto sandbox = RunningSandbox("runtime-a", {"tcp:21006:50090"});
    sandbox.clear_id();
    *listed.add_sandboxes() = sandbox;

    PortManager::ReservationMap reservations;
    const auto status =
        SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(listed, reservations);

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_EQ(reservations.at("runtime-a"), std::vector<int>({21006}));
}

TEST(SandboxdExecutorTest, ReusableRestoreMetadataIsValidatedAndBuildsExactPhysicalIdentity)
{
    ::messages::ReusableSnapshotRestore restore;
    restore.set_snapshotid("snapshot-42");
    restore.set_allowlogicalinstanceidrebind(true);
    restore.mutable_artifact()->set_storagebackend("obs");
    restore.mutable_artifact()->set_objectkey(
        "reusable/v1/tenant-hash/snapshot-42/checkpoint.img");
    restore.mutable_artifact()->set_size(4096);
    restore.mutable_artifact()->set_sha256(std::string(64, 'a'));
    restore.mutable_artifact()->set_format("gvisor-checkpoint");
    restore.mutable_artifact()->set_formatversion(1);
    EXPECT_TRUE(resume_identity::ValidateReusableSnapshotRestore(restore));

    messages::StartInstanceRequest request;
    auto *info = request.mutable_runtimeinstanceinfo();
    info->set_instanceid("clone-instance");
    info->set_requestid("clone-attempt");
    info->set_runtimeid(resume_identity::RuntimeID("clone-instance", "clone-attempt"));
    (*info->mutable_runtimeconfig()->mutable_posixenvs())[YR_TENANT_ID] = "tenant-a";
    *info->mutable_reusablesnapshotrestore() = restore;

    const auto identity = SandboxdExecutor::ConsumeRestoreIdentity(request);
    EXPECT_TRUE(identity.trusted);
    EXPECT_TRUE(identity.reusable);
    EXPECT_FALSE(identity.rejected);
    EXPECT_EQ(identity.snapshotID, "snapshot-42");
    EXPECT_EQ(identity.labels.at("instance_id"), "clone-instance");
    EXPECT_EQ(identity.labels.at("request_id"), "clone-attempt");
    EXPECT_EQ(identity.labels.at("runtime_id"), info->runtimeid());
    EXPECT_EQ(identity.labels.at("source_snapshot_id"), "snapshot-42");
    EXPECT_EQ(identity.labels.at("target_attempt_id"), "clone-attempt");
    EXPECT_TRUE(SandboxdExecutor::IsRestoreRequest(*info));

    restore.mutable_artifact()->set_sha256("bad");
    EXPECT_FALSE(resume_identity::ValidateReusableSnapshotRestore(restore));
}

TEST(SandboxdExecutorTest, TrustedResumePreallocatesPhysicalPortsThroughPortManager)
{
    const std::string runtimeID = "trusted-resume-runtime";
    PortManager::GetInstance().InitPortResource(21000, 10);
    SandboxdExecutor executor("SandboxdResumePortPreallocationExecutor", litebus::AID(),
                              "/tmp/sandboxd-resume-port-preallocation-checkpoints");
    auto request = std::make_shared<messages::StartInstanceRequest>();
    auto *info = request->mutable_runtimeinstanceinfo();
    info->set_runtimeid(runtimeID);
    (*info->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_NETWORK] =
        R"({"portForwardings":[{"port":50090,"protocol":"tcp"}]})";

    SandboxdStartParams params;
    params.runtimeID = runtimeID;
    executor.ApplyPortForwardMappings(&params, request);

    ASSERT_EQ(params.portMappings.size(), 1u);
    EXPECT_EQ(params.portMappings.front(), "tcp:21000:50090");
    EXPECT_EQ(PortManager::GetInstance().GetPorts(runtimeID), std::vector<int>({21000}));

    SandboxdStartParams replayParams;
    replayParams.runtimeID = runtimeID;
    executor.ApplyPortForwardMappings(&replayParams, request);
    ASSERT_EQ(replayParams.portMappings.size(), 1u);
    EXPECT_EQ(replayParams.portMappings.front(), params.portMappings.front());
    EXPECT_EQ(PortManager::GetInstance().GetPorts(runtimeID), std::vector<int>({21000}));
    PortManager::GetInstance().ReleasePorts(runtimeID);
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, StartupListRebuildsAllPhysicalPortsAtomically)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    PortManager::GetInstance().BeginReconcile();
    runtime::v1::ListSandboxesResponse listed;
    *listed.add_sandboxes() = RunningSandbox("runtime-a", {"tcp:21001:50090"});
    *listed.add_sandboxes() = RunningSandbox("runtime-b", {"udp:21003:8765"});

    PortManager::ReservationMap reservations;
    const auto status = SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(listed, reservations);

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts(reservations));
    EXPECT_TRUE(PortManager::GetInstance().IsReady());
    EXPECT_EQ(PortManager::GetInstance().GetPorts("runtime-a"), std::vector<int>({21001}));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("runtime-b"), std::vector<int>({21003}));
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, PortAllocationIsClosedUntilStartupReconciliationCompletes)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    PortManager::GetInstance().BeginReconcile();
    EXPECT_FALSE(PortManager::GetInstance().IsReady());
    EXPECT_TRUE(PortManager::GetInstance().RequestPorts("blocked-runtime", 1).empty());
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts({}));
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("ready-runtime", 1),
              std::vector<int>({21000}));
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, ExistingPortManagerRequestGetAndReleaseBehaviorIsPreserved)
{
    PortManager::GetInstance().InitPortResource(21000, 4);

    EXPECT_TRUE(PortManager::GetInstance().GetPorts("runtime-a").empty());
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("runtime-a", 2),
              std::vector<int>({21000, 21001}));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("runtime-a"),
              std::vector<int>({21000, 21001}));
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("runtime-a", 2),
              std::vector<int>({21000, 21001}));
    EXPECT_TRUE(PortManager::GetInstance().RequestPorts("runtime-a", 1).empty());

    PortManager::GetInstance().ReleasePorts("runtime-a");
    EXPECT_TRUE(PortManager::GetInstance().GetPorts("runtime-a").empty());
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("runtime-b", 2),
              std::vector<int>({21000, 21001}));
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, FailedStartupRebuildKeepsAllocationClosedAndPublishesNoPartialCache)
{
    PortManager::GetInstance().InitPortResource(21000, 4);
    PortManager::GetInstance().BeginReconcile();

    const PortManager::ReservationMap invalid{
        {"runtime-a", {21000}},
        {"runtime-b", {21000}},
    };
    EXPECT_FALSE(PortManager::GetInstance().RebuildPorts(invalid));
    EXPECT_FALSE(PortManager::GetInstance().IsReady());
    EXPECT_TRUE(PortManager::GetInstance().GetPorts("runtime-a").empty());
    EXPECT_TRUE(PortManager::GetInstance().GetPorts("runtime-b").empty());
    EXPECT_TRUE(PortManager::GetInstance().RequestPorts("new-runtime", 1).empty());
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, ExactSandboxReadThroughDoesNotAllocateASecondPortSet)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    runtime::v1::ListSandboxesResponse listed;
    *listed.add_sandboxes() = RunningSandbox("existing-runtime", {"tcp:21002:50090"});
    PortManager::ReservationMap reservations;
    ASSERT_TRUE(SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(listed, reservations).IsOk());
    PortManager::GetInstance().BeginReconcile();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts(reservations));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("existing-runtime"), std::vector<int>({21002}));
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("new-runtime", 1),
              std::vector<int>({21000}));

    PortManager::GetInstance().BeginReconcile();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts(reservations));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("existing-runtime"), std::vector<int>({21002}));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("new-runtime"), std::vector<int>{});
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, StartupReconciliationRejectsDuplicateAndMalformedPhysicalPorts)
{
    runtime::v1::ListSandboxesResponse duplicate;
    *duplicate.add_sandboxes() = RunningSandbox("runtime-a", {"tcp:21001:50090"});
    *duplicate.add_sandboxes() = RunningSandbox("runtime-b", {"tcp:21001:50091"});
    PortManager::ReservationMap reservations;
    EXPECT_TRUE(SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(duplicate, reservations).IsError());

    runtime::v1::ListSandboxesResponse malformed;
    *malformed.add_sandboxes() = RunningSandbox("runtime-c", {"tcp:21002"});
    EXPECT_TRUE(SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(malformed, reservations).IsError());

    runtime::v1::ListSandboxesResponse missingIdentity;
    auto sandbox = RunningSandbox("runtime-d", {"tcp:21003:50090"});
    sandbox.mutable_labels()->erase("runtime_id");
    *missingIdentity.add_sandboxes() = sandbox;
    EXPECT_TRUE(SandboxdExecutor::BuildPortReservationsFromPhysicalFacts(missingIdentity, reservations).IsError());
}

TEST(SandboxdExecutorTest, RuntimeReconcileKeepsSandboxdPortFactsWhenEtcdMappingsAreStale)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    PortManager::GetInstance().BeginReconcile();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts({
        {"runtime-a", {21001}},
        {"runtime-b", {21002}},
    }));

    auto request = std::make_shared<messages::ReconcileRuntimesRequest>();
    request->set_requestid("reconcile-stale-etcd-ports");
    auto *runtimeA = request->add_entries();
    runtimeA->set_runtimeid("runtime-a");
    runtimeA->set_containerid("sandbox-a");
    runtimeA->set_portmappings(R"(["public+http:21002:50090"])");
    auto *runtimeB = request->add_entries();
    runtimeB->set_runtimeid("runtime-b");
    runtimeB->set_containerid("sandbox-b");
    runtimeB->set_portmappings(R"(["public+http:21002:50091"])");

    auto listed = std::make_shared<runtime::v1::ListSandboxesResponse>();
    *listed->add_sandboxes() = RunningSandbox(
        "runtime-a", {"tcp:21001:50090"}, "instance-a", "request-a", "sandbox-a");
    *listed->add_sandboxes() = RunningSandbox(
        "runtime-b", {"tcp:21002:50091"}, "instance-b", "request-b", "sandbox-b");
    SandboxdExecutor executor(
        "SandboxdPhysicalPortAuthorityExecutor", litebus::AID(), "/tmp/sandboxd-physical-port-authority");
    messages::RuntimeInstanceInfo runtimeAInfo;
    runtimeAInfo.set_runtimeid("runtime-a");
    executor.stateManager_.Register({"runtime-a", "sandbox-a", {}, {}, runtimeAInfo});
    executor.stateManager_.MarkStartDone("runtime-a");
    messages::RuntimeInstanceInfo runtimeBInfo;
    runtimeBInfo.set_runtimeid("runtime-b");
    executor.stateManager_.Register({"runtime-b", "sandbox-b", {}, {}, runtimeBInfo});
    executor.stateManager_.MarkStartDone("runtime-b");

    const auto response = executor.OnReconcileRuntimes(request, listed);

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("runtime-a"), std::vector<int>({21001}));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("runtime-b"), std::vector<int>({21002}));
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, ExactPhysicalCacheConflictFailsWithoutStealingOtherRuntime)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    PortManager::GetInstance().BeginReconcile();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts({{"physical-owner", {21001}}}));
    SandboxdExecutor executor("SandboxdExactConflictExecutor", litebus::AID(),
                              "/tmp/sandboxd-exact-conflict-checkpoints");
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->set_runtimeid("exact-runtime");
    (*request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_NETWORK] =
        R"({"portForwardings":[{"port":50090,"protocol":"tcp"}]})";

    const auto status = executor.ApplyExistingSandboxPhysicalPortMappings(
        "exact-runtime", request, {"tcp:21001:50090"});

    EXPECT_TRUE(status.IsError());
    EXPECT_EQ(PortManager::GetInstance().GetPorts("physical-owner"), std::vector<int>({21001}));
    EXPECT_TRUE(PortManager::GetInstance().GetPorts("exact-runtime").empty());
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, ExactPhysicalFactReplacesOnlyTheSameRuntimeTemporaryCache)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    PortManager::GetInstance().BeginReconcile();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts({
        {"exact-runtime", {21000}},
        {"other-runtime", {21002}},
    }));
    SandboxdExecutor executor("SandboxdExactReadThroughExecutor", litebus::AID(),
                              "/tmp/sandboxd-exact-read-through-checkpoints");
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->set_runtimeid("exact-runtime");
    (*request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_NETWORK] =
        R"({"portForwardings":[{"port":50090,"protocol":"tcp"}]})";

    const auto status = executor.ApplyExistingSandboxPhysicalPortMappings(
        "exact-runtime", request, {"tcp:21001:50090"});

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_EQ(PortManager::GetInstance().GetPorts("exact-runtime"), std::vector<int>({21001}));
    EXPECT_EQ(PortManager::GetInstance().GetPorts("other-runtime"), std::vector<int>({21002}));
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("new-runtime", 1), std::vector<int>({21000}));
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, FirstRestoreRejectsPortsDifferentFromPreallocation)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    ASSERT_EQ(PortManager::GetInstance().RequestPorts("fresh-runtime", 1),
              std::vector<int>({21000}));
    SandboxdExecutor executor("SandboxdFreshRestorePortExecutor", litebus::AID(),
                              "/tmp/sandboxd-fresh-restore-port-checkpoints");
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->set_runtimeid("fresh-runtime");
    (*request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_NETWORK] =
        R"({"portForwardings":[{"port":50090,"protocol":"tcp"}]})";

    const auto status = executor.ApplySandboxPhysicalPortMappings(
        "fresh-runtime", request, {"tcp:21001:50090"});

    EXPECT_TRUE(status.IsError());
    EXPECT_EQ(PortManager::GetInstance().GetPorts("fresh-runtime"), std::vector<int>({21000}));
    PortManager::GetInstance().ReleasePorts("fresh-runtime");
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(SandboxdExecutorTest, DeleteReleasesOnlyTheExactRuntimePorts)
{
    PortManager::GetInstance().InitPortResource(21000, 10);
    PortManager::GetInstance().BeginReconcile();
    ASSERT_TRUE(PortManager::GetInstance().RebuildPorts({
        {"runtime-a", {21000, 21001}},
        {"runtime-b", {21002}},
    }));
    PortManager::GetInstance().ReleasePorts("runtime-a");
    EXPECT_TRUE(PortManager::GetInstance().GetPorts("runtime-a").empty());
    EXPECT_EQ(PortManager::GetInstance().GetPorts("runtime-b"), std::vector<int>({21002}));
    EXPECT_EQ(PortManager::GetInstance().RequestPorts("runtime-c", 2),
              std::vector<int>({21000, 21001}));
    PortManager::GetInstance().InitPortResource(500, 2000);
}

TEST(CheckpointPlanTest, UserManagedAndInstanceManagedRequestsUseTheSameExplicitPlanContract)
{
    messages::SnapshotRuntimeRequest userRequest;
    userRequest.set_requestid("snapshot-request");
    userRequest.set_runtimeid("runtime-1");
    userRequest.set_snapshotid("snapshot-1");
    userRequest.set_checkpointdir("/checkpoints/user/snapshot-1");
    userRequest.set_ttl(600);

    CheckpointPlan userPlan;
    auto status = BuildCheckpointPlan(userRequest, "sandbox-1", ArtifactLifecycle::USER_MANAGED,
                                      false, userPlan);
    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_EQ(userPlan.sandboxID, "sandbox-1");
    EXPECT_EQ(userPlan.checkpointID, "snapshot-1");
    EXPECT_EQ(userPlan.checkpointDirectory, "/checkpoints/user/snapshot-1");
    EXPECT_EQ(userPlan.lifecycle, ArtifactLifecycle::USER_MANAGED);
    EXPECT_FALSE(userPlan.leaveRuntimeRunning);

    auto pauseRequest = userRequest;
    pauseRequest.set_type(common::PAUSE_RESUME);
    pauseRequest.set_snapshotid("pause-1");
    pauseRequest.set_checkpointdir("/checkpoints/pause/tenant/instance/pause-1");
    CheckpointPlan pausePlan;
    status = BuildCheckpointPlan(pauseRequest, "sandbox-1", ArtifactLifecycle::INSTANCE_MANAGED,
                                 true, pausePlan);
    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_EQ(pausePlan.sandboxID, userPlan.sandboxID);
    EXPECT_EQ(pausePlan.checkpointID, "pause-1");
    EXPECT_EQ(pausePlan.lifecycle, ArtifactLifecycle::INSTANCE_MANAGED);
    EXPECT_TRUE(pausePlan.leaveRuntimeRunning);
}

TEST(CheckpointPlanTest, ReusableFinalizeBuildsExactDeleteCheckpointIdentityWithoutStoppingSandbox)
{
    SandboxInfo source;
    source.runtimeID = "runtime-1";
    source.sandboxID = "sandbox-1";
    source.instanceInfo.set_instanceid("instance-1");
    source.instanceInfo.set_runtimeid("runtime-1");

    ::messages::SnapshotAttemptFinalizeRequest request;
    request.set_protocolversion(1);
    request.set_operation(::messages::REUSABLE_SNAPSHOT_COMMITTED);
    request.set_instanceid("instance-1");
    request.set_runtimeid("runtime-1");
    request.set_snapshotid("snapshot-1");
    request.set_attemptid("attempt-1");
    request.set_expectedsize(4096);
    request.set_expectedsha256("sha256-value");

    std::string checkpointDirectory;
    auto status = SandboxdExecutor::BuildReusableSnapshotCleanupIdentity(
        request, source, "/checkpoints", checkpointDirectory);

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_EQ(checkpointDirectory, "/checkpoints/snapshot-1");
    EXPECT_EQ(source.sandboxID, "sandbox-1");

    request.set_runtimeid("other-runtime");
    status = SandboxdExecutor::BuildReusableSnapshotCleanupIdentity(
        request, source, "/checkpoints", checkpointDirectory);
    EXPECT_TRUE(status.IsError());

    request.set_runtimeid("runtime-1");
    request.set_snapshotid("../escape");
    status = SandboxdExecutor::BuildReusableSnapshotCleanupIdentity(
        request, source, "/checkpoints", checkpointDirectory);
    EXPECT_TRUE(status.IsError());
}

TEST(CheckpointPlanTest, RejectsImplicitOrUnsafeCheckpointIdentity)
{
    messages::SnapshotRuntimeRequest request;
    request.set_requestid("pause-request");
    request.set_runtimeid("runtime-1");
    request.set_snapshotid("pause-1");
    CheckpointPlan plan;

    auto status = BuildCheckpointPlan(request, "sandbox-1", ArtifactLifecycle::INSTANCE_MANAGED,
                                      true, plan);
    EXPECT_TRUE(status.IsError());
    EXPECT_NE(status.RawMessage().find("directory"), std::string::npos);

    request.set_checkpointdir("relative/checkpoint");
    status = BuildCheckpointPlan(request, "sandbox-1", ArtifactLifecycle::INSTANCE_MANAGED,
                                 true, plan);
    EXPECT_TRUE(status.IsError());

    request.set_checkpointdir("/checkpoints/pause-1");
    request.set_snapshotid("../escape");
    status = BuildCheckpointPlan(request, "sandbox-1", ArtifactLifecycle::INSTANCE_MANAGED,
                                 true, plan);
    EXPECT_TRUE(status.IsError());
}

}  // namespace functionsystem::test
