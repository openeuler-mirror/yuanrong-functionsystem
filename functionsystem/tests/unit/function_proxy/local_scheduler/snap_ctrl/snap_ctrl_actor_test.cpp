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

#include "function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor.h"
#include "function_proxy/local_scheduler/snap_ctrl/resume_attempt_registry.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/async.hpp"
#include "common/snapshot_storage/obs_snapshot_storage.h"
#include "common/types/instance_state.h"
#include "common/utils/resume_identity.h"
#include "function_master/global_scheduler/traefik_route_cache.h"
#include "function_proxy/common/posix_client/control_plane_client/control_interface_posix_client.h"
#include "function_proxy/common/state_machine/instance_context.h"
#include "function_proxy/local_scheduler/tcp_tunnel_server.h"
#include "mocks/mock_control_interface_client_manager_proxy.h"
#include "mocks/mock_function_agent_mgr.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_instance_control_view.h"
#include "mocks/mock_local_sched_srv.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
namespace {

using namespace ::testing;
using namespace local_scheduler;

constexpr char OWNER_PROXY_ID[] = "pause-owner-proxy";
constexpr char OTHER_PROXY_ID[] = "pause-other-proxy";
constexpr char INSTANCE_ID[] = "pause-instance";
constexpr char INSTANCE_REQUEST_ID[] = "create-pause-instance";
constexpr int64_t INSTANCE_VERSION = 17;
constexpr char SNAPSHOT_PROBE_MESSAGE[] = "snapshot runtime probe failure";

runtime::PrepareSnapResponse MakePrepareResponse(common::ErrorCode code, const std::string &message)
{
    runtime::PrepareSnapResponse response;
    response.set_code(code);
    response.set_message(message);
    return response;
}

class ControllablePrepareSnapClient final : public ControlInterfacePosixClient {
public:
    ControllablePrepareSnapClient() : BaseClient(nullptr), ControlInterfacePosixClient(nullptr)
    {
        Reset();
    }

    litebus::Future<runtime::PrepareSnapResponse> PrepareSnap(runtime::PrepareSnapRequest &&) override
    {
        prepareCalls_.fetch_add(1);
        if (prepareTransportFailuresRemaining_.load() > 0) {
            prepareTransportFailuresRemaining_.fetch_sub(1);
            litebus::Future<runtime::PrepareSnapResponse> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed;
        }
        std::lock_guard<std::mutex> guard(lock_);
        return attempt_->promise.GetFuture();
    }

    litebus::Future<runtime::SnapStartedResponse> SnapStarted(runtime::SnapStartedRequest &&) override
    {
        snapStartedCalls_.fetch_add(1);
        if (operations_ != nullptr) {
            operations_->emplace_back("snapstarted");
        }
        if (snapStartedFutureFailuresRemaining_.load() > 0) {
            snapStartedFutureFailuresRemaining_.fetch_sub(1);
            litebus::Future<runtime::SnapStartedResponse> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed;
        }
        if (pendingSnapStarted_ != nullptr) {
            return pendingSnapStarted_->GetFuture();
        }
        runtime::SnapStartedResponse response;
        response.set_code(snapStartedCode_);
        response.set_message(snapStartedCode_ == common::ERR_NONE ? "" : "SnapStarted failed");
        return response;
    }

    void ConfigureSnapStarted(common::ErrorCode code, std::vector<std::string> *operations)
    {
        snapStartedCode_ = code;
        operations_ = operations;
    }

    void ConfigureSnapStartedFutureFailures(int failures)
    {
        snapStartedFutureFailuresRemaining_ = failures;
    }

    void MakeSnapStartedPending()
    {
        pendingSnapStarted_ = std::make_shared<litebus::Promise<runtime::SnapStartedResponse>>();
    }

    void CompleteSnapStarted(common::ErrorCode code)
    {
        runtime::SnapStartedResponse response;
        response.set_code(code);
        response.set_message(code == common::ERR_NONE ? "" : "SnapStarted failed");
        pendingSnapStarted_->SetValue(response);
    }

    int SnapStartedCalls() const { return snapStartedCalls_.load(); }

    int PrepareCalls() const
    {
        return prepareCalls_.load();
    }

    void ConfigurePrepareTransportFailures(int failures)
    {
        prepareTransportFailuresRemaining_ = failures;
    }

    void Complete(const runtime::PrepareSnapResponse &response)
    {
        std::shared_ptr<Attempt> attempt;
        {
            std::lock_guard<std::mutex> guard(lock_);
            attempt = attempt_;
            if (attempt->settled) {
                return;
            }
            attempt->settled = true;
        }
        attempt->promise.SetValue(response);
    }

    void FailOutstanding()
    {
        Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "test cleanup"));
    }

    void Reset()
    {
        std::lock_guard<std::mutex> guard(lock_);
        attempt_ = std::make_shared<Attempt>();
    }

private:
    struct Attempt {
        litebus::Promise<runtime::PrepareSnapResponse> promise;
        bool settled = false;
    };

    std::atomic<int> prepareCalls_ { 0 };
    std::atomic<int> prepareTransportFailuresRemaining_ { 0 };
    std::atomic<int> snapStartedCalls_ { 0 };
    std::atomic<int> snapStartedFutureFailuresRemaining_ { 0 };
    common::ErrorCode snapStartedCode_ { common::ERR_NONE };
    std::vector<std::string> *operations_ { nullptr };
    std::mutex lock_;
    std::shared_ptr<Attempt> attempt_;
    std::shared_ptr<litebus::Promise<runtime::SnapStartedResponse>> pendingSnapStarted_;
};

class SnapshotRuntimeProbe final : public MockFunctionAgentMgr {
public:
    SnapshotRuntimeProbe() : MockFunctionAgentMgr("pause-snapshot-runtime-probe", nullptr)
    {
    }

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::string &requestID, const resource_view::InstanceInfo &, int32_t) override
    {
        calls_.fetch_add(1);
        messages::SnapshotRuntimeResponse response;
        response.set_requestid(requestID);
        response.set_code(common::ERR_INNER_COMMUNICATION);
        response.set_message(SNAPSHOT_PROBE_MESSAGE);
        return response;
    }

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntimeAnonymous(
        const std::string &requestID, const resource_view::InstanceInfo &instanceInfo,
        const std::string &snapshotID) override
    {
        anonymousCalls_.fetch_add(1);
        anonymousSnapshotID_ = snapshotID;
        if (operations_ != nullptr) {
            operations_->emplace_back("checkpoint");
        }
        messages::SnapshotRuntimeResponse response;
        response.set_requestid(requestID);
        response.set_code(anonymousCode_);
        response.set_message(anonymousCode_ == common::ERR_NONE ? "" : "anonymous checkpoint failed");
        if (anonymousCode_ == common::ERR_NONE) {
            auto *snapshot = response.mutable_localsnapshot();
            snapshot->set_snapshotid(snapshotID);
            snapshot->set_localrecoverycandidate(true);
            snapshot->set_instanceid(instanceInfo.instanceid());
            snapshot->set_size(4096);
            snapshot->set_createdatunixseconds(1);
        }
        return response;
    }

    void ConfigureAnonymousCheckpoint(int32_t code, std::vector<std::string> *operations)
    {
        anonymousCode_ = code;
        operations_ = operations;
    }

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::string &requestID, const resource_view::InstanceInfo &instanceInfo, int32_t ttl,
        common::SnapType type, const std::string &snapshotID, const std::string &checkpointDir) override
    {
        pauseCalls_.fetch_add(1);
        pauseRequestID_ = requestID;
        pauseSnapshotID_ = snapshotID;
        pauseSnapshotIDs_.emplace_back(snapshotID);
        pauseCheckpointDir_ = checkpointDir;
        pauseTtl_ = ttl;
        pauseType_ = type;
        if (operations_ != nullptr) {
            operations_->emplace_back("checkpoint");
        }
        if (pauseFutureFailuresRemaining_.load() > 0) {
            pauseFutureFailuresRemaining_.fetch_sub(1);
            litebus::Future<messages::SnapshotRuntimeResponse> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed;
        }
        if (pendingPauseResponse_ != nullptr) {
            return pendingPauseResponse_->GetFuture();
        }
        messages::SnapshotRuntimeResponse response;
        response.set_requestid(requestID);
        if (agentPersistedPause_) {
            response.set_code(common::ERR_NONE);
            auto *snapshot = response.mutable_snapshotinfo();
            snapshot->set_checkpointid(snapshotID);
            snapshot->set_storage(pauseStorageBackend_);
            snapshot->set_size(4096);
            snapshot->set_sha256("agent-persisted-pause-sha256");
            snapshot->set_createtime("1700000000");
            snapshot->set_ttlseconds(ttl);
            snapshot->set_status(resources::SNAPSHOT_READY);
            if (reusableSnapshot_) {
                auto *artifact = response.mutable_reusablesnapshotartifact();
                artifact->set_storagebackend("obs");
                artifact->set_objectkey("reusable/v1/tenant/snapshot/checkpoint.img");
                artifact->set_size(4096);
                artifact->set_sha256("agent-persisted-pause-sha256");
                artifact->set_format("sandboxd-checkpoint");
                artifact->set_formatversion(1);
            }
            return response;
        }
        const auto responseIndex = pauseResponseIndex_++;
        const auto responseState = pausePhysicalStates_.empty()
                                       ? pausePhysicalState_
                                       : pausePhysicalStates_[std::min(responseIndex, pausePhysicalStates_.size() - 1)];
        const auto responseCode = pauseCodes_.empty()
                                      ? pauseCode_
                                      : pauseCodes_[std::min(responseIndex, pauseCodes_.size() - 1)];
        response.set_code(responseCode);
        response.set_message(pauseMessage_);
        response.mutable_snapshotinfo()->set_checkpointid(snapshotID);
        response.mutable_snapshotinfo()->set_storage(pauseStorageBackend_);
        if (reusableFailureWithArtifact_) {
            auto *artifact = response.mutable_reusablesnapshotartifact();
            artifact->set_storagebackend("obs");
            artifact->set_objectkey("reusable/v1/tenant/failure/checkpoint.img");
            artifact->set_size(4096);
            artifact->set_sha256("agent-persisted-pause-sha256");
            artifact->set_format("sandboxd-checkpoint");
            artifact->set_formatversion(1);
        }
        if (responseCode != common::ERR_NONE) {
            response.mutable_physicalfact()->set_state(responseState);
        }
        return response;
    }

    litebus::Future<::messages::SnapshotAttemptFinalizeResponse> FinalizeSnapshotAttempt(
        const resource_view::InstanceInfo &,
        const ::messages::SnapshotAttemptFinalizeRequest &request) override
    {
        finalizeRequests_.emplace_back(request);
        if (operations_ != nullptr
            && (request.operation() == ::messages::REUSABLE_SNAPSHOT_COMMITTED
                || request.operation() == ::messages::REUSABLE_SNAPSHOT_ABORTED)) {
            operations_->emplace_back("finalize");
        }
        if (pendingFinalizeResponse_ != nullptr) {
            return pendingFinalizeResponse_->GetFuture();
        }
        if (!queuedFinalizeResponses_.empty()) {
            auto response = queuedFinalizeResponses_.front();
            queuedFinalizeResponses_.erase(queuedFinalizeResponses_.begin());
            response.set_attemptid(request.attemptid());
            return response;
        }
        ::messages::SnapshotAttemptFinalizeResponse response;
        if (request.operation() == ::messages::RESUME_ABORTED) {
            response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
            response.set_localcleanupcomplete(true);
        } else {
            response.set_code(finalizeCode_);
            response.set_resultunknown(finalizeResultUnknown_);
            response.set_localcleanupcomplete(finalizeLocalComplete_);
            response.set_remotecleanupcomplete(finalizeRemoteComplete_);
        }
        response.set_attemptid(request.attemptid());
        return response;
    }

    litebus::Future<::messages::SnapshotAttemptFinalizeResponse> FinalizeSnapshotAttemptOnAnyAgent(
        const ::messages::SnapshotAttemptFinalizeRequest &request) override
    {
        resource_view::InstanceInfo unused;
        return FinalizeSnapshotAttempt(unused, request);
    }

    void HoldFinalizeResponse()
    {
        pendingFinalizeResponse_ =
            std::make_shared<litebus::Promise<::messages::SnapshotAttemptFinalizeResponse>>();
    }

    void CompleteFinalizeResponse(int32_t code, bool localComplete, bool remoteComplete,
                                  bool resultUnknown = false)
    {
        ::messages::SnapshotAttemptFinalizeResponse response;
        response.set_code(code);
        response.set_resultunknown(resultUnknown);
        response.set_localcleanupcomplete(localComplete);
        response.set_remotecleanupcomplete(remoteComplete);
        response.set_attemptid(finalizeRequests_.empty() ? "" : finalizeRequests_.back().attemptid());
        pendingFinalizeResponse_->SetValue(response);
        pendingFinalizeResponse_.reset();
    }

    void SetFinalizeResponse(int32_t code, bool localComplete, bool remoteComplete,
                             bool resultUnknown = false)
    {
        finalizeCode_ = code;
        finalizeResultUnknown_ = resultUnknown;
        finalizeLocalComplete_ = localComplete;
        finalizeRemoteComplete_ = remoteComplete;
    }

    void QueueFinalizeResponse(int32_t code, bool localComplete, bool remoteComplete,
                               bool resultUnknown = false)
    {
        ::messages::SnapshotAttemptFinalizeResponse response;
        response.set_code(code);
        response.set_resultunknown(resultUnknown);
        response.set_localcleanupcomplete(localComplete);
        response.set_remotecleanupcomplete(remoteComplete);
        queuedFinalizeResponses_.emplace_back(std::move(response));
    }

    void ConfigureAgentPersistedPauseSuccess(std::vector<std::string> *operations)
    {
        operations_ = operations;
        pauseCode_ = common::ERR_NONE;
        pauseMessage_.clear();
        pausePhysicalState_ = runtime::v1::SANDBOX_STATE_RUNNING;
        agentPersistedPause_ = true;
    }

    void ConfigureReusableSnapshotSuccess(std::vector<std::string> *operations)
    {
        ConfigureAgentPersistedPauseSuccess(operations);
        reusableSnapshot_ = true;
    }

    void ConfigureReusableSnapshotFailureWithArtifact(std::vector<std::string> *operations)
    {
        ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                              "injected reusable publish failure", operations);
        reusableFailureWithArtifact_ = true;
    }

    void ConfigurePauseFailure(runtime::v1::SandboxState state, std::string message,
                               std::vector<std::string> *operations)
    {
        operations_ = operations;
        pauseCode_ = common::ERR_INNER_COMMUNICATION;
        pauseMessage_ = std::move(message);
        pausePhysicalState_ = state;
    }

    void ConfigurePauseFailureSequence(std::vector<runtime::v1::SandboxState> states,
                                       std::string message, std::vector<std::string> *operations)
    {
        ConfigurePauseFailure(states.front(), std::move(message), operations);
        pausePhysicalStates_ = std::move(states);
        pauseCodes_.assign(pausePhysicalStates_.size(), common::ERR_INNER_COMMUNICATION);
        pauseResponseIndex_ = 0;
    }

    void ConfigurePauseStorageBackend(std::string backend)
    {
        pauseStorageBackend_ = std::move(backend);
    }

    void ConfigurePauseFutureFailures(int failures)
    {
        pauseFutureFailuresRemaining_ = failures;
    }

    void HoldPauseResponse()
    {
        pendingPauseResponse_ = std::make_shared<litebus::Promise<messages::SnapshotRuntimeResponse>>();
    }

    void CompletePauseResponse(const messages::SnapshotRuntimeResponse &response)
    {
        pendingPauseResponse_->SetValue(response);
        pendingPauseResponse_.reset();
    }

    messages::SnapshotRuntimeResponse AgentPersistedPauseResponse() const
    {
        messages::SnapshotRuntimeResponse response;
        response.set_requestid(pauseRequestID_);
        response.set_code(common::ERR_NONE);
        auto *snapshot = response.mutable_snapshotinfo();
        snapshot->set_checkpointid(pauseSnapshotID_);
        snapshot->set_storage(pauseStorageBackend_);
        snapshot->set_size(4096);
        snapshot->set_sha256("agent-persisted-pause-sha256");
        snapshot->set_createtime("1700000000");
        snapshot->set_ttlseconds(pauseTtl_);
        snapshot->set_status(resources::SNAPSHOT_READY);
        return response;
    }

    int Calls() const
    {
        return calls_.load();
    }

    int PauseCalls() const { return pauseCalls_.load(); }
    int AnonymousCalls() const { return anonymousCalls_.load(); }
    const std::string &AnonymousSnapshotID() const { return anonymousSnapshotID_; }
    const std::string &PauseSnapshotID() const { return pauseSnapshotID_; }
    const std::vector<std::string> &PauseSnapshotIDs() const { return pauseSnapshotIDs_; }
    const std::string &PauseCheckpointDir() const { return pauseCheckpointDir_; }
    common::SnapType PauseType() const { return pauseType_; }
    int32_t PauseTtl() const { return pauseTtl_; }
    const std::vector<::messages::SnapshotAttemptFinalizeRequest> &FinalizeRequests() const
    {
        return finalizeRequests_;
    }

private:
    std::atomic<int> calls_ { 0 };
    std::atomic<int> pauseCalls_ { 0 };
    std::atomic<int> anonymousCalls_ { 0 };
    std::atomic<int> pauseFutureFailuresRemaining_ { 0 };
    std::string pauseRequestID_;
    std::string anonymousSnapshotID_;
    int32_t anonymousCode_ { common::ERR_NONE };
    std::string pauseSnapshotID_;
    std::vector<std::string> pauseSnapshotIDs_;
    std::vector<runtime::v1::SandboxState> pausePhysicalStates_;
    std::vector<common::ErrorCode> pauseCodes_;
    size_t pauseResponseIndex_ { 0 };
    std::string pauseCheckpointDir_;
    std::string pauseMessage_;
    std::string pauseStorageBackend_ { "obs" };
    std::vector<std::string> *operations_ { nullptr };
    common::ErrorCode pauseCode_ { common::ERR_INNER_COMMUNICATION };
    runtime::v1::SandboxState pausePhysicalState_ { runtime::v1::SANDBOX_STATE_UNKNOWN };
    bool agentPersistedPause_ { false };
    bool reusableSnapshot_ { false };
    bool reusableFailureWithArtifact_ { false };
    std::shared_ptr<litebus::Promise<messages::SnapshotRuntimeResponse>> pendingPauseResponse_;
    common::SnapType pauseType_ { common::DUMPSTATE };
    int32_t pauseTtl_ { -1 };
    std::vector<::messages::SnapshotAttemptFinalizeRequest> finalizeRequests_;
    std::vector<::messages::SnapshotAttemptFinalizeResponse> queuedFinalizeResponses_;
    std::shared_ptr<litebus::Promise<::messages::SnapshotAttemptFinalizeResponse>> pendingFinalizeResponse_;
    int32_t finalizeCode_ { static_cast<int32_t>(StatusCode::SUCCESS) };
    bool finalizeResultUnknown_ { false };
    bool finalizeLocalComplete_ { true };
    bool finalizeRemoteComplete_ { true };
};

class ResumeBoundarySnapshotStorage final : public snapshot_storage::SnapshotStorage {
public:
    static constexpr char PAYLOAD[] = "resume boundary checkpoint";

    explicit ResumeBoundarySnapshotStorage(std::vector<std::string> *operations = nullptr,
                                           int64_t sourceVersion = 11)
        : operations_(operations), sourceVersion_(sourceVersion)
    {
    }

    litebus::Future<snapshot_storage::SnapshotStat> Stat(const std::string &) override
    {
        statCalls_.fetch_add(1);
        if (statStatus_.IsError()) {
            return snapshot_storage::SnapshotStat{ statStatus_, {} };
        }
        return snapshot_storage::SnapshotStat{
            Status::OK(), { "snapshot-a", sourceVersion_, sizeof(PAYLOAD) - 1,
                            resume_identity::Sha256Hex(PAYLOAD), true } };
    }
    litebus::Future<Status> Get(const std::string &, const std::string &destination) override
    {
        getCalls_.fetch_add(1);
        std::ofstream(destination, std::ios::binary | std::ios::trunc) << PAYLOAD;
        return Status::OK();
    }
    litebus::Future<Status> PutTemporary(const std::string &, const std::string &,
                                         const snapshot_storage::SnapshotObjectMetadata &) override
    {
        return Status(StatusCode::FAILED, "unexpected put");
    }
    litebus::Future<Status> Publish(const std::string &, const std::string &,
                                    const snapshot_storage::SnapshotObjectMetadata &) override
    {
        return Status(StatusCode::FAILED, "unexpected publish");
    }
    litebus::Future<Status> Delete(const std::string &key) override
    {
        if (operations_ != nullptr) {
            operations_->emplace_back("delete:" + key);
        }
        const auto call = deleteCalls_.fetch_add(1);
        return call < deleteFailures_ ? Status(StatusCode::FAILED, "injected cleanup failure") : deleteStatus_;
    }

    void SetDeleteFailures(int failures) { deleteFailures_ = failures; }
    void SetDeleteStatus(const Status &status) { deleteStatus_ = status; }
    void SetStatStatus(const Status &status) { statStatus_ = status; }
    int DeleteCalls() const { return deleteCalls_.load(); }
    int StatCalls() const { return statCalls_.load(); }
    int GetCalls() const { return getCalls_.load(); }

private:
    std::vector<std::string> *operations_;
    Status statStatus_ = Status::OK();
    std::atomic<int> statCalls_ { 0 };
    std::atomic<int> getCalls_ { 0 };
    std::atomic<int> deleteCalls_ { 0 };
    int deleteFailures_ { 0 };
    Status deleteStatus_ = Status::OK();
    int64_t sourceVersion_;
};

class InMemoryObsSnapshotClient final : public snapshot_storage::ObsSnapshotClient {
public:
    Status MultipartUpload(const std::string &key, const std::string &,
                           const snapshot_storage::SnapshotObjectMetadata &metadata) override
    {
        std::lock_guard<std::mutex> guard(lock_);
        objects_[key] = metadata;
        return Status::OK();
    }

    snapshot_storage::ObsHeadResult Head(const std::string &key) override
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto iter = objects_.find(key);
        if (iter == objects_.end()) {
            return { Status(StatusCode::FILE_NOT_FOUND), {}, {} };
        }
        return { Status::OK(), iter->second, "etag-" + key };
    }

    Status ConditionalCopy(const std::string &temporaryKey, const std::string &finalKey,
                           const std::string &, const snapshot_storage::SnapshotObjectMetadata &metadata) override
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (objects_.find(temporaryKey) == objects_.end()) {
            return Status(StatusCode::FILE_NOT_FOUND);
        }
        objects_[finalKey] = metadata;
        return Status::OK();
    }

    Status Download(const std::string &, const std::string &) override
    {
        return Status(StatusCode::FAILED, "not used");
    }

    Status Delete(const std::string &key) override
    {
        std::lock_guard<std::mutex> guard(lock_);
        objects_.erase(key);
        return Status::OK();
    }

private:
    std::mutex lock_;
    std::unordered_map<std::string, snapshot_storage::SnapshotObjectMetadata> objects_;
};

resources::InstanceInfo MakeInstanceInfo(InstanceState state = InstanceState::RUNNING,
                                         const std::string &owner = OWNER_PROXY_ID)
{
    resources::InstanceInfo info;
    info.set_instanceid(INSTANCE_ID);
    info.set_requestid(INSTANCE_REQUEST_ID);
    info.set_functionproxyid(owner);
    info.set_version(INSTANCE_VERSION);
    info.mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    if (state == InstanceState::RUNNING) {
        info.set_containerid("container-a");
    }
    return info;
}

class PauseStateMachineProbe final : public InstanceStateMachine {
public:
    PauseStateMachineProbe(const std::shared_ptr<InstanceContext> &context)
        : InstanceStateMachine(OWNER_PROXY_ID, context, false)
    {
    }

    litebus::Future<TransitionResult> TransitionTo(const TransContext &context) override
    {
        if (context.newState != InstanceState::PAUSED) {
            return InstanceStateMachine::TransitionTo(context);
        }
        pauseTransitions.fetch_add(1);
        transitionVersion = context.version;
        if (operations != nullptr) {
            operations->emplace_back("cas");
        }
        if (context.scheduleReq != nullptr) {
            pausedScheduleRequest = std::make_shared<messages::ScheduleRequest>(*context.scheduleReq);
        }
        if (commitPausedBeforeResult && context.scheduleReq != nullptr) {
            auto committed = context.scheduleReq->instance();
            committed.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
            committed.set_version(context.version + 1);
            committed.set_functionproxyid(INSTANCE_MANAGER_OWNER);
            committed.clear_runtimeid();
            committed.clear_functionagentid();
            committed.clear_containerid();
            committed.clear_containerip();
            committed.clear_unitid();
            committed.clear_runtimeaddress();
            committed.clear_proxygrpcaddress();
            authoritativeInfo.CopyFrom(committed);
            hasAuthoritativeInfo = true;
            if (!keepLocalRunningAfterCommit) {
                UpdateInstanceInfo(committed);
            }
        } else if (replaceGenerationBeforeResult) {
            auto replaced = GetInstanceInfo();
            replaced.set_runtimeid("replacement-runtime-after-cas");
            UpdateInstanceInfo(replaced);
        }
        if (transitionFutureError) {
            litebus::Future<TransitionResult> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed;
        }
        TransitionResult result;
        if (transitionFailuresRemaining.load() > 0) {
            transitionFailuresRemaining.fetch_sub(1);
            result.status = Status(StatusCode::SCHEDULE_CONFLICTED, "transient pause CAS conflict");
            result.version = context.version;
            return result;
        }
        result.status = transitionStatus;
        result.version = context.version + 1;
        return result;
    }

    litebus::Future<resources::InstanceInfo> SyncInstanceFromMetaStore() override
    {
        syncCalls.fetch_add(1);
        if (syncFutureFailuresRemaining.load() > 0) {
            syncFutureFailuresRemaining.fetch_sub(1);
            litebus::Future<resources::InstanceInfo> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed;
        }
        return hasAuthoritativeInfo ? authoritativeInfo : GetInstanceInfo();
    }

    std::atomic<int> pauseTransitions { 0 };
    int64_t transitionVersion { -1 };
    Status transitionStatus = Status::OK();
    bool commitPausedBeforeResult { false };
    bool keepLocalRunningAfterCommit { false };
    bool replaceGenerationBeforeResult { false };
    bool transitionFutureError { false };
    bool hasAuthoritativeInfo { false };
    std::atomic<int> syncCalls { 0 };
    std::atomic<int> syncFutureFailuresRemaining { 0 };
    std::atomic<int> transitionFailuresRemaining { 0 };
    resources::InstanceInfo authoritativeInfo;
    std::vector<std::string> *operations { nullptr };
    std::shared_ptr<messages::ScheduleRequest> pausedScheduleRequest;
};

std::shared_ptr<PauseStateMachineProbe> MakeInstanceStateMachine(const resources::InstanceInfo &info)
{
    auto scheduleRequest = std::make_shared<messages::ScheduleRequest>();
    scheduleRequest->set_requestid(info.requestid());
    scheduleRequest->mutable_instance()->CopyFrom(info);
    auto context = std::make_shared<InstanceContext>(scheduleRequest);
    auto stateMachine = std::make_shared<PauseStateMachineProbe>(context);
    stateMachine->SetVersion(info.version());
    return stateMachine;
}

std::string MakeSnapshotPayload(common::SnapType type, const std::string &name = "")
{
    core_service::SnapOptions options;
    options.set_type(type);
    if (type == common::PAUSE_RESUME) {
        options.set_ttl(90'000);
    }
    options.set_name(name);
    return options.SerializeAsString();
}

class SnapCtrlActorPauseContextTest : public Test {
public:
    void SetUp() override
    {
        stateMachine_ = MakeInstanceStateMachine(MakeInstanceInfo());
        instanceControlView_ = std::make_shared<MockInstanceControlView>(OWNER_PROXY_ID);
        EXPECT_CALL(*instanceControlView_, GetInstance(INSTANCE_ID))
            .Times(AnyNumber())
            .WillRepeatedly(Return(stateMachine_));

        prepareClient_ = std::make_shared<ControllablePrepareSnapClient>();
        clientManager_ = std::make_shared<MockControlInterfaceClientManagerProxy>();
        EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
            .Times(AnyNumber())
            .WillRepeatedly(Return(prepareClient_));

        snapshotRuntimeProbe_ = std::make_shared<SnapshotRuntimeProbe>();
        instanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        ON_CALL(*instanceCtrl_, BeginPauseGate(testing::_)).WillByDefault(Return(Status::OK()));
        ON_CALL(*instanceCtrl_, ReleasePausedInstanceResources(testing::_))
            .WillByDefault(Return(Status::OK()));
        ON_CALL(*instanceCtrl_, TransInstanceState(testing::_, testing::_))
            .WillByDefault(Invoke([](const std::shared_ptr<InstanceStateMachine> &machine,
                                     const TransContext &context) {
                return machine->TransitionTo(context);
            }));
        EXPECT_CALL(*instanceCtrl_, BeginPauseGate(testing::_)).Times(AnyNumber());
        EXPECT_CALL(*instanceCtrl_, ReleasePausedInstanceResources(testing::_)).Times(AnyNumber());
        actor_ = std::make_shared<SnapCtrlActor>("pause-context-test", OWNER_PROXY_ID, pauseRetryPolicy_);
        actor_->BindInstanceControlView(instanceControlView_);
        actor_->BindClientManager(clientManager_);
        actor_->BindFunctionAgentMgr(snapshotRuntimeProbe_);
        actor_->BindInstanceCtrl(instanceCtrl_);
        litebus::Spawn(actor_);
    }

    void TearDown() override
    {
        prepareClient_->FailOutstanding();
        for (auto &request : requests_) {
            (void)request.WaitFor(5000);
        }
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_->GetAID());
        actor_ = nullptr;
        snapshotRuntimeProbe_ = nullptr;
        instanceCtrl_ = nullptr;
        clientManager_ = nullptr;
        prepareClient_ = nullptr;
        instanceControlView_ = nullptr;
        stateMachine_ = nullptr;
    }

protected:
    litebus::Future<KillResponse> HandleSnapshot(const std::string &requestID, common::SnapType type)
    {
        auto request = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot, requestID,
                                      std::string(INSTANCE_ID), MakeSnapshotPayload(type));
        requests_.emplace_back(request);
        return request;
    }

    void FlushActor()
    {
        auto flushed = litebus::Async(actor_->GetAID(), &SnapCtrlActor::Sync);
        ASSERT_AWAIT_READY(flushed);
        ASSERT_TRUE(flushed.Get().IsOk());
    }

    void UpdateInstanceInfo(const std::function<void(resources::InstanceInfo &)> &update)
    {
        auto info = stateMachine_->GetInstanceInfo();
        update(info);
        stateMachine_->UpdateInstanceInfo(info);
    }

    std::shared_ptr<PauseStateMachineProbe> stateMachine_;
    std::shared_ptr<MockInstanceControlView> instanceControlView_;
    std::shared_ptr<ControllablePrepareSnapClient> prepareClient_;
    std::shared_ptr<MockControlInterfaceClientManagerProxy> clientManager_;
    std::shared_ptr<SnapshotRuntimeProbe> snapshotRuntimeProbe_;
    std::shared_ptr<MockInstanceCtrl> instanceCtrl_;
    std::shared_ptr<SnapCtrlActor> actor_;
    std::vector<litebus::Future<KillResponse>> requests_;
    PauseRetryPolicy pauseRetryPolicy_;
};

TEST_F(SnapCtrlActorPauseContextTest, AnonymousCheckpointPreparesCheckpointsAndCallsSnapStarted)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAnonymousCheckpoint(common::ERR_NONE, &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    auto request = litebus::Async(
        actor_->GetAID(), &SnapCtrlActor::HandleAnonymousCheckpoint,
        std::string("anonymous-request"), std::string(INSTANCE_ID), uint64_t{0});
    requests_.emplace_back(request);

    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5'000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, ""));
    ASSERT_AWAIT_READY_FOR(request, 5'000);

    EXPECT_EQ(request.Get().code(), common::ERR_NONE);
    EXPECT_EQ(snapshotRuntimeProbe_->AnonymousCalls(), 1);
    EXPECT_THAT(snapshotRuntimeProbe_->AnonymousSnapshotID(), testing::StartsWith("anon-"));
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
    EXPECT_EQ(operations, (std::vector<std::string>{"checkpoint", "snapstarted"}));
    EXPECT_EQ(stateMachine_->GetInstanceState(), InstanceState::RUNNING);
}

TEST_F(SnapCtrlActorPauseContextTest, AnonymousCheckpointFailureStillCallsSnapStarted)
{
    snapshotRuntimeProbe_->ConfigureAnonymousCheckpoint(common::ERR_INNER_COMMUNICATION, nullptr);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, nullptr);
    auto request = litebus::Async(
        actor_->GetAID(), &SnapCtrlActor::HandleAnonymousCheckpoint,
        std::string("anonymous-failure"), std::string(INSTANCE_ID), uint64_t{0});
    requests_.emplace_back(request);

    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5'000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, ""));
    ASSERT_AWAIT_READY_FOR(request, 5'000);

    EXPECT_EQ(request.Get().code(), common::ERR_INNER_COMMUNICATION);
    EXPECT_EQ(snapshotRuntimeProbe_->AnonymousCalls(), 1);
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
    EXPECT_EQ(stateMachine_->GetInstanceState(), InstanceState::RUNNING);
}

TEST_F(SnapCtrlActorPauseContextTest, AnonymousCheckpointRetriesSnapStartedAfterStreamReconnect)
{
    snapshotRuntimeProbe_->ConfigureAnonymousCheckpoint(common::ERR_NONE, nullptr);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, nullptr);
    prepareClient_->ConfigureSnapStartedFutureFailures(1);
    auto request = litebus::Async(
        actor_->GetAID(), &SnapCtrlActor::HandleAnonymousCheckpoint,
        std::string("anonymous-reconnect"), std::string(INSTANCE_ID), uint64_t{0});
    requests_.emplace_back(request);

    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5'000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, ""));
    ASSERT_AWAIT_READY_FOR(request, 5'000);

    EXPECT_EQ(request.Get().code(), common::ERR_NONE);
    EXPECT_EQ(snapshotRuntimeProbe_->AnonymousCalls(), 1);
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 2);
    EXPECT_EQ(stateMachine_->GetInstanceState(), InstanceState::RUNNING);
}

TEST_F(SnapCtrlActorPauseContextTest, AuthorizedDeleteWaitersSharePauseTerminalBeforeGateCompletes)
{
    auto gate = std::make_shared<litebus::Promise<Status>>();
    EXPECT_CALL(*instanceCtrl_, BeginPauseGate(testing::_))
        .WillOnce(Return(gate->GetFuture()));

    auto pause = HandleSnapshot("pause-with-delete-waiter", common::PAUSE_RESUME);
    FlushActor();
    auto firstWaiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                      std::string(INSTANCE_ID));
    auto secondWaiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                       std::string(INSTANCE_ID));
    ASSERT_AWAIT_NO_SET_FOR(pause, 50);
    ASSERT_AWAIT_NO_SET_FOR(firstWaiter, 50);
    ASSERT_AWAIT_NO_SET_FOR(secondWaiter, 50);

    gate->SetValue(Status(StatusCode::FAILED, "gate failed"));
    ASSERT_AWAIT_READY(pause);
    ASSERT_AWAIT_READY(firstWaiter);
    ASSERT_AWAIT_READY(secondWaiter);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_NE(firstWaiter.Get().generation, 0U);
    EXPECT_EQ(firstWaiter.Get().generation, secondWaiter.Get().generation);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), firstWaiter.Get().generation);
    ASSERT_AWAIT_READY(finished);
    EXPECT_TRUE(finished.Get().IsOk());
}

enum class MissingPauseDependency {
    INSTANCE_CTRL,
    CLIENT_MANAGER,
    FUNCTION_AGENT,
};

class SnapCtrlActorPauseDependencyTest : public SnapCtrlActorPauseContextTest,
                                         public WithParamInterface<MissingPauseDependency> {
};

TEST_P(SnapCtrlActorPauseDependencyTest, MissingDependencyFailsBeforePauseGate)
{
    switch (GetParam()) {
        case MissingPauseDependency::INSTANCE_CTRL:
            actor_->BindInstanceCtrl(nullptr);
            break;
        case MissingPauseDependency::CLIENT_MANAGER:
            actor_->BindClientManager(nullptr);
            break;
        case MissingPauseDependency::FUNCTION_AGENT:
            actor_->BindFunctionAgentMgr(nullptr);
            break;
    }
    Mock::VerifyAndClearExpectations(instanceCtrl_.get());
    ON_CALL(*instanceCtrl_, BeginPauseGate(testing::_))
        .WillByDefault(Return(Status(StatusCode::FAILED, "must not enter gate")));
    EXPECT_CALL(*instanceCtrl_, BeginPauseGate(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-missing-dependency", common::PAUSE_RESUME);

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(response.Get().message(), HasSubstr("depend"));
}

std::string MissingPauseDependencyName(const TestParamInfo<MissingPauseDependency> &info)
{
    switch (info.param) {
        case MissingPauseDependency::INSTANCE_CTRL:
            return "InstanceCtrl";
        case MissingPauseDependency::CLIENT_MANAGER:
            return "ClientManager";
        case MissingPauseDependency::FUNCTION_AGENT:
            return "FunctionAgent";
    }
    return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(Dependencies, SnapCtrlActorPauseDependencyTest,
                         Values(MissingPauseDependency::INSTANCE_CTRL,
                                MissingPauseDependency::CLIENT_MANAGER,
                                MissingPauseDependency::FUNCTION_AGENT),
                         MissingPauseDependencyName);

TEST_F(SnapCtrlActorPauseContextTest, GetClientTransportFailureRetriesSamePauseContext)
{
    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> failedClient;
    failedClient.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    Mock::VerifyAndClearExpectations(clientManager_.get());
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
        .WillOnce(Return(failedClient))
        .WillRepeatedly(Return(prepareClient_));
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "checkpoint after client retry", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-client-transport-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().message(), "checkpoint after client retry");
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 1);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 1);
}

TEST_F(SnapCtrlActorPauseContextTest, AuthorizedDeleteCancelsGetClientTransportRetry)
{
    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> failedClient;
    failedClient.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    std::atomic<int> getClientCalls { 0 };
    std::atomic<bool> allowTestCleanup { false };
    Mock::VerifyAndClearExpectations(clientManager_.get());
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this, &failedClient, &getClientCalls, &allowTestCleanup](const std::string &) {
            getClientCalls.fetch_add(1);
            if (!allowTestCleanup.load()) {
                return failedClient;
            }
            return litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>(prepareClient_);
        }));

    auto pause = HandleSnapshot("pause-client-transport-delete", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([&getClientCalls]() { return getClientCalls.load() >= 2; }, 5000);
    auto deleteWaiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                       std::string(INSTANCE_ID));

    const auto deleteStatus = deleteWaiter.WaitFor(500);
    EXPECT_TRUE(deleteStatus.IsOK()) << "authorized Delete must stop the Prepare client retry loop";
    if (!deleteStatus.IsOK()) {
        allowTestCleanup.store(true);
        prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "test cleanup"));
        ASSERT_AWAIT_READY(pause);
        ASSERT_AWAIT_READY(deleteWaiter);
        return;
    }

    ASSERT_AWAIT_READY_FOR(pause, 500);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

class SnapCtrlActorPauseDeadlineTest : public SnapCtrlActorPauseContextTest {
public:
    void SetUp() override
    {
        pauseRetryPolicy_ = PauseRetryPolicy{ .initialDelayMs = 5,
                                              .maximumDelayMs = 20,
                                              .operationTimeoutMs = 50 };
        SnapCtrlActorPauseContextTest::SetUp();
    }
};

TEST_F(SnapCtrlActorPauseDeadlineTest, GetClientTransportRetryStopsAtOperationDeadline)
{
    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> failedClient;
    failedClient.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    std::atomic<int> getClientCalls { 0 };
    std::atomic<bool> allowTestCleanup { false };
    Mock::VerifyAndClearExpectations(clientManager_.get());
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this, &failedClient, &getClientCalls, &allowTestCleanup](const std::string &) {
            getClientCalls.fetch_add(1);
            if (!allowTestCleanup.load()) {
                return failedClient;
            }
            return litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>(prepareClient_);
        }));
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));

    auto pause = HandleSnapshot("pause-client-transport-deadline", common::PAUSE_RESUME);
    const auto pauseStatus = pause.WaitFor(500);
    EXPECT_TRUE(pauseStatus.IsOK()) << "Pause must stop retrying when its operation deadline expires";
    if (!pauseStatus.IsOK()) {
        allowTestCleanup.store(true);
        prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "test cleanup"));
        ASSERT_AWAIT_READY(pause);
        return;
    }

    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_THAT(pause.Get().message(), HasSubstr("deadline"));
    EXPECT_GE(getClientCalls.load(), 2);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

class SnapCtrlActorPauseSlowRetryTest : public SnapCtrlActorPauseContextTest {
public:
    void SetUp() override
    {
        pauseRetryPolicy_ = PauseRetryPolicy{ .initialDelayMs = 1'000,
                                              .maximumDelayMs = 1'000,
                                              .operationTimeoutMs = 5'000 };
        SnapCtrlActorPauseContextTest::SetUp();
    }
};

TEST_F(SnapCtrlActorPauseSlowRetryTest, AuthorizedDeleteActivelyCancelsPrepareRetry)
{
    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> failedClient;
    failedClient.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    std::atomic<int> getClientCalls { 0 };
    Mock::VerifyAndClearExpectations(clientManager_.get());
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([&failedClient, &getClientCalls](const std::string &) {
            getClientCalls.fetch_add(1);
            return failedClient;
        }));

    auto pause = HandleSnapshot("pause-active-delete-cancel", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([&getClientCalls]() { return getClientCalls.load() == 1; }, 500);
    auto deleteWaiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                       std::string(INSTANCE_ID));

    const auto deleteStatus = deleteWaiter.WaitFor(100);
    EXPECT_TRUE(deleteStatus.IsOK())
        << "authorized Delete must actively cancel Prepare instead of waiting for its retry timer";
    if (!deleteStatus.IsOK()) {
        ASSERT_AWAIT_READY_FOR(deleteWaiter, 2'000);
    }
    ASSERT_AWAIT_READY(pause);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, PrepareTransportFailureRetriesSamePauseContext)
{
    std::vector<std::string> operations;
    prepareClient_->ConfigurePrepareTransportFailures(1);
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "checkpoint after prepare retry", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-prepare-transport-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 2; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().message(), "checkpoint after prepare retry");
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 1);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 1);
}

TEST_F(SnapCtrlActorPauseContextTest, AuthorizedDeleteCancelsPermanentPrepareTransportFailure)
{
    prepareClient_->ConfigurePrepareTransportFailures(std::numeric_limits<int>::max());

    auto pause = HandleSnapshot("pause-prepare-transport-delete", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() >= 2; }, 500);
    auto deleteWaiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                       std::string(INSTANCE_ID));

    ASSERT_AWAIT_READY_FOR(deleteWaiter, 100);
    ASSERT_AWAIT_READY_FOR(pause, 100);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

TEST_F(SnapCtrlActorPauseDeadlineTest, PermanentPrepareTransportFailureStopsAtOperationDeadline)
{
    prepareClient_->ConfigurePrepareTransportFailures(std::numeric_limits<int>::max());
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));

    auto pause = HandleSnapshot("pause-prepare-transport-deadline", common::PAUSE_RESUME);

    ASSERT_AWAIT_READY_FOR(pause, 500);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_THAT(pause.Get().message(), HasSubstr("deadline"));
    EXPECT_GE(prepareClient_->PrepareCalls(), 2);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, ActorShutdownCompletesPendingPauseWithoutLateCallback)
{
    auto pause = HandleSnapshot("pause-actor-shutdown", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);

    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());

    ASSERT_AWAIT_READY_FOR(pause, 100);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_THAT(pause.Get().message(), HasSubstr("shut down"));

    // The external RPC can still finish after actor teardown. Its callback
    // must be discarded and must not replace the terminal caller result.
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "late prepared"));
    EXPECT_THAT(pause.Get().message(), HasSubstr("shut down"));
}

TEST_F(SnapCtrlActorPauseSlowRetryTest, StalePrepareTimerCannotCompleteNextPauseContext)
{
    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> failedClient;
    failedClient.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    std::atomic<bool> useHealthyClient { false };
    std::atomic<int> getClientCalls { 0 };
    Mock::VerifyAndClearExpectations(clientManager_.get());
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this, &failedClient, &useHealthyClient, &getClientCalls](const std::string &) {
            getClientCalls.fetch_add(1);
            return useHealthyClient.load()
                ? litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>(prepareClient_)
                : failedClient;
        }));

    auto firstPause = HandleSnapshot("pause-stale-timer-first", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([&getClientCalls]() { return getClientCalls.load() == 1; }, 500);
    FlushActor();
    auto deleteWaiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                       std::string(INSTANCE_ID));
    ASSERT_AWAIT_READY_FOR(deleteWaiter, 100);
    ASSERT_AWAIT_READY_FOR(firstPause, 100);
    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), deleteWaiter.Get().generation);
    ASSERT_AWAIT_READY(finished);
    ASSERT_TRUE(finished.Get().IsOk());

    useHealthyClient.store(true);
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));
    auto secondPause = HandleSnapshot("pause-stale-timer-second", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 500);

    ASSERT_AWAIT_NO_SET_FOR(secondPause, 1'200);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 1);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_PARAM_INVALID, "second pause stopped"));
    ASSERT_AWAIT_READY(secondPause);
    EXPECT_EQ(secondPause.Get().message(), "second pause stopped");
}

TEST_F(SnapCtrlActorPauseContextTest, ExplicitPrepareErrorRecoversGateBeforeReturningOriginalError)
{
    std::vector<std::string> operations;
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &) {
            operations.emplace_back("recover");
            return litebus::Future<Status>(Status::OK());
        }));

    auto response = HandleSnapshot("pause-prepare-rejected", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_PARAM_INVALID, "prepare rejected"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_PARAM_INVALID);
    EXPECT_EQ(response.Get().message(), "prepare rejected");
    EXPECT_THAT(operations, ElementsAre("recover"));
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, ExplicitPrepareErrorRetriesFailedRecoverFutureAndStatus)
{
    litebus::Future<Status> failedRecover;
    failedRecover.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .WillOnce(Return(failedRecover))
        .WillOnce(Return(Status(StatusCode::FAILED, "recover rejected")))
        .WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-prepare-recover-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_PARAM_INVALID, "prepare original error"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_PARAM_INVALID);
    EXPECT_EQ(response.Get().message(), "prepare original error");
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, SnapshotRuntimeFutureFailureRetriesSameSnapshotIdentity)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "checkpoint after future retry", &operations);
    snapshotRuntimeProbe_->ConfigurePauseFutureFailures(1);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-snapshot-future-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().message(), "checkpoint after future retry");
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 2);
    ASSERT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 2);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs()[0], snapshotRuntimeProbe_->PauseSnapshotIDs()[1]);
}

TEST_F(SnapCtrlActorPauseContextTest, LateGateSuccessAfterGenerationChangeDoesNotPrepareRuntime)
{
    auto gate = std::make_shared<litebus::Promise<Status>>();
    EXPECT_CALL(*instanceCtrl_, BeginPauseGate(testing::_))
        .WillOnce(Return(gate->GetFuture()));

    auto pause = HandleSnapshot("pause-late-gate", common::PAUSE_RESUME);
    FlushActor();
    ASSERT_AWAIT_NO_SET_FOR(pause, 50);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("replacement-runtime"); });
    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "must not prepare"));
    gate->SetValue(Status::OK());

    ASSERT_AWAIT_READY(pause);
    EXPECT_NE(pause.Get().code(), common::ERR_NONE);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, NextPauseDeletesRetainedSnapshotBeforeCheckpoint)
{
    snapshotRuntimeProbe_->HoldFinalizeResponse();
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-retained-snapshot");
        auto *snapshot = info.mutable_snapshotinfo();
        snapshot->set_checkpointid("snapshot-retained-after-resume");
        snapshot->set_status(resources::SNAPSHOT_READY);
        snapshot->set_storage("obs");
        snapshot->set_size(8192);
        snapshot->set_sha256("retained-snapshot-sha256");
        snapshot->set_createtime("1786672800");
        snapshot->set_ttlseconds(90000);
    });

    Mock::VerifyAndClearExpectations(instanceCtrl_.get());
    std::atomic<int> gateCalls { 0 };
    EXPECT_CALL(*instanceCtrl_, BeginPauseGate(testing::_))
        .WillOnce(Invoke([&gateCalls](const resources::InstanceInfo &) {
            gateCalls.fetch_add(1);
            return Status(StatusCode::FAILED, "stop after retained snapshot cleanup");
        }));

    auto response = HandleSnapshot("pause-after-resume", common::PAUSE_RESUME);
    FlushActor();

    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    const auto &cleanup = snapshotRuntimeProbe_->FinalizeRequests().front();
    EXPECT_EQ(cleanup.operation(), ::messages::PAUSED_DELETED);
    EXPECT_EQ(cleanup.tenantid(), "tenant-retained-snapshot");
    EXPECT_EQ(cleanup.instanceid(), INSTANCE_ID);
    EXPECT_EQ(cleanup.snapshotid(), "snapshot-retained-after-resume");
    EXPECT_EQ(cleanup.expectedsize(), 8192U);
    EXPECT_EQ(cleanup.expectedsha256(), "retained-snapshot-sha256");
    EXPECT_EQ(gateCalls.load(), 0);
    ASSERT_AWAIT_NO_SET_FOR(response, 50);

    snapshotRuntimeProbe_->CompleteFinalizeResponse(
        static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(gateCalls.load(), 1);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, PausedSnapshotDeleteRoutesExactImmutableCleanupThroughFunctionAgent)
{
    snapshotRuntimeProbe_->SetFinalizeResponse(
        static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    auto info = MakeInstanceInfo(InstanceState::PAUSED, "");
    info.set_tenantid("tenant-a");
    auto *snapshot = info.mutable_snapshotinfo();
    snapshot->set_checkpointid("snapshot-paused-delete");
    snapshot->set_status(resources::SNAPSHOT_READY);
    snapshot->set_storage("obs");
    snapshot->set_size(4096);
    snapshot->set_sha256("paused-delete-sha256");
    snapshot->set_createtime("1786672800");
    snapshot->set_ttlseconds(90000);

    auto deleted = litebus::Async(actor_->GetAID(), &SnapCtrlActor::DeletePauseSnapshot, info);

    ASSERT_AWAIT_READY(deleted);
    EXPECT_TRUE(deleted.Get().IsOk());
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    const auto &request = snapshotRuntimeProbe_->FinalizeRequests().front();
    EXPECT_EQ(static_cast<int32_t>(request.operation()), 5);
    EXPECT_EQ(request.tenantid(), "tenant-a");
    EXPECT_EQ(request.instanceid(), INSTANCE_ID);
    EXPECT_EQ(request.snapshotid(), "snapshot-paused-delete");
    EXPECT_EQ(request.expectedsize(), 4096U);
    EXPECT_EQ(request.expectedsha256(), "paused-delete-sha256");
}

TEST_F(SnapCtrlActorPauseContextTest, MissingPausedSnapshotDeleteIsIdempotentSuccess)
{
    snapshotRuntimeProbe_->SetFinalizeResponse(
        static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    auto info = MakeInstanceInfo(InstanceState::PAUSED, "");
    info.set_tenantid("tenant-a");

    auto deleted = litebus::Async(actor_->GetAID(), &SnapCtrlActor::DeletePauseSnapshot, info);

    ASSERT_AWAIT_READY(deleted);
    EXPECT_TRUE(deleted.Get().IsOk());
    EXPECT_TRUE(snapshotRuntimeProbe_->FinalizeRequests().empty());
}

TEST_F(SnapCtrlActorPauseContextTest, PausedSnapshotStorageErrorIsReturned)
{
    snapshotRuntimeProbe_->SetFinalizeResponse(
        static_cast<int32_t>(StatusCode::FAILED), true, false);
    auto info = MakeInstanceInfo(InstanceState::PAUSED, "");
    info.set_tenantid("tenant-a");
    auto *snapshot = info.mutable_snapshotinfo();
    snapshot->set_checkpointid("snapshot-paused-delete-error");
    snapshot->set_status(resources::SNAPSHOT_READY);
    snapshot->set_storage("obs");
    snapshot->set_size(4096);
    snapshot->set_sha256("paused-delete-error-sha256");

    auto deleted = litebus::Async(actor_->GetAID(), &SnapCtrlActor::DeletePauseSnapshot, info);

    ASSERT_AWAIT_READY(deleted);
    EXPECT_EQ(deleted.Get().StatusCode(), StatusCode::FAILED);
    EXPECT_TRUE(deleted.Get().IsError());
}

TEST_F(SnapCtrlActorPauseContextTest, PausedSnapshotDeleteRejectsMissingTenant)
{
    auto info = MakeInstanceInfo(InstanceState::PAUSED, "");

    auto deleted = litebus::Async(actor_->GetAID(), &SnapCtrlActor::DeletePauseSnapshot, info);

    ASSERT_AWAIT_READY(deleted);
    EXPECT_TRUE(deleted.Get().IsError());
    EXPECT_THAT(deleted.Get().GetMessage(), HasSubstr("tenant"));
    EXPECT_TRUE(snapshotRuntimeProbe_->FinalizeRequests().empty());
}

TEST_F(SnapCtrlActorPauseContextTest, PausedSnapshotDeleteRejectsMissingInstance)
{
    auto info = MakeInstanceInfo(InstanceState::PAUSED, "");
    info.set_tenantid("tenant-a");
    info.clear_instanceid();

    auto deleted = litebus::Async(actor_->GetAID(), &SnapCtrlActor::DeletePauseSnapshot, info);

    ASSERT_AWAIT_READY(deleted);
    EXPECT_TRUE(deleted.Get().IsError());
    EXPECT_THAT(deleted.Get().GetMessage(), HasSubstr("instance"));
    EXPECT_TRUE(snapshotRuntimeProbe_->FinalizeRequests().empty());
}

TEST_F(SnapCtrlActorPauseContextTest, PauseResumeRunningOwnerCoalescesDuplicatePendingRequests)
{
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .Times(2)
        .WillRepeatedly(Return(Status::OK()));
    auto first = HandleSnapshot("pause-first", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    ASSERT_AWAIT_NO_SET_FOR(first, 50);

    auto duplicate = HandleSnapshot("pause-first", common::PAUSE_RESUME);
    FlushActor();
    EXPECT_EQ(prepareClient_->PrepareCalls(), 1);
    ASSERT_AWAIT_NO_SET_FOR(first, 50);
    ASSERT_AWAIT_NO_SET_FOR(duplicate, 50);

    auto conflicting = HandleSnapshot("pause-conflicting", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(conflicting);
    EXPECT_NE(conflicting.Get().code(), common::ERR_NONE);

    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "prepare failed"));
    ASSERT_AWAIT_READY(first);
    ASSERT_AWAIT_READY(duplicate);
    EXPECT_NE(first.Get().code(), common::ERR_NONE);
    EXPECT_EQ(duplicate.Get().code(), first.Get().code());
    EXPECT_EQ(duplicate.Get().message(), first.Get().message());
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);

    prepareClient_->Reset();
    auto retry = HandleSnapshot("pause-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 2; }, 5000);
    ASSERT_AWAIT_NO_SET_FOR(retry, 50);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "retry prepare failed"));
    ASSERT_AWAIT_READY(retry);
}

TEST_F(SnapCtrlActorPauseContextTest, PauseResumeRejectsNonOwnerBeforePrepare)
{
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_functionproxyid(OTHER_PROXY_ID); });
    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "must not prepare"));

    auto response = HandleSnapshot("pause-non-owner", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, PauseResumeRejectsMismatchedInstanceIDBeforePrepare)
{
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_instanceid("replacement-instance"); });
    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "must not prepare"));

    auto response = HandleSnapshot("pause-mismatched-instance", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, PauseResumeRejectsEmptyInstanceRequestIDBeforePrepare)
{
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.clear_requestid(); });
    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "must not prepare"));

    auto response = HandleSnapshot("pause-empty-instance-request", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, ProxyRestartReplaysPauseCommittedFromAuthoritativePaused)
{
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
        info.set_functionproxyid(INSTANCE_MANAGER_OWNER);
        info.clear_runtimeid();
        info.clear_runtimeaddress();
        info.clear_functionagentid();
        info.clear_containerid();
        info.clear_containerip();
        info.clear_unitid();
        info.clear_proxygrpcaddress();
        auto *snapshot = info.mutable_snapshotinfo();
        snapshot->set_checkpointid("pause-already-complete");
        snapshot->set_status(resources::SNAPSHOT_READY);
        snapshot->set_storage("obs");
        snapshot->set_size(4096);
        snapshot->set_sha256("paused-replay-sha256");
        snapshot->set_createtime("1700000000");
        snapshot->set_ttlseconds(90'000);
    });

    auto response = HandleSnapshot("pause-already-complete", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    core_service::SnapInfo result;
    ASSERT_TRUE(result.ParseFromString(response.Get().payload()));
    EXPECT_EQ(result.snapshotid(), "pause-already-complete");
    EXPECT_EQ(result.size(), 4096);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    const auto &finalize = snapshotRuntimeProbe_->FinalizeRequests().front();
    EXPECT_EQ(finalize.operation(), ::messages::PAUSE_COMMITTED);
    EXPECT_EQ(finalize.snapshotid(), "pause-already-complete");
    EXPECT_EQ(finalize.attemptid(), "pause-already-complete");
}

enum class InvalidPausedSnapshotMetadata {
    UNSET,
    UNSPECIFIED,
    DELETING,
    READY_WITHOUT_CHECKPOINT_ID,
    OWNER_MISSING,
    OWNER_MISMATCH,
};

class SnapCtrlActorPausedSnapshotValidationTest : public SnapCtrlActorPauseContextTest,
                                                   public WithParamInterface<InvalidPausedSnapshotMetadata> {
};

TEST_P(SnapCtrlActorPausedSnapshotValidationTest, PauseResumeRejectsInvalidPausedMetadataBeforePrepare)
{
    UpdateInstanceInfo([metadata = GetParam()](resources::InstanceInfo &info) {
        info.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
        info.set_functionproxyid(INSTANCE_MANAGER_OWNER);
        info.clear_runtimeid();
        info.clear_runtimeaddress();
        info.clear_functionagentid();
        info.clear_containerid();
        info.clear_containerip();
        info.clear_unitid();
        info.clear_proxygrpcaddress();
        switch (metadata) {
            case InvalidPausedSnapshotMetadata::UNSET:
                info.clear_snapshotinfo();
                break;
            case InvalidPausedSnapshotMetadata::UNSPECIFIED:
                info.mutable_snapshotinfo()->set_checkpointid("paused-checkpoint");
                info.mutable_snapshotinfo()->set_status(resources::SNAPSHOT_STATUS_UNSPECIFIED);
                break;
            case InvalidPausedSnapshotMetadata::DELETING:
                info.mutable_snapshotinfo()->set_checkpointid("paused-checkpoint");
                info.mutable_snapshotinfo()->set_status(resources::SNAPSHOT_DELETING);
                break;
            case InvalidPausedSnapshotMetadata::READY_WITHOUT_CHECKPOINT_ID:
                info.mutable_snapshotinfo()->set_status(resources::SNAPSHOT_READY);
                break;
            case InvalidPausedSnapshotMetadata::OWNER_MISSING:
                info.clear_functionproxyid();
                info.mutable_snapshotinfo()->set_checkpointid("paused-checkpoint");
                info.mutable_snapshotinfo()->set_status(resources::SNAPSHOT_READY);
                break;
            case InvalidPausedSnapshotMetadata::OWNER_MISMATCH:
                info.set_functionproxyid(OTHER_PROXY_ID);
                info.mutable_snapshotinfo()->set_checkpointid("paused-checkpoint");
                info.mutable_snapshotinfo()->set_status(resources::SNAPSHOT_READY);
                break;
        }
    });

    auto response = HandleSnapshot("pause-invalid-metadata", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

std::string InvalidPausedSnapshotMetadataName(const TestParamInfo<InvalidPausedSnapshotMetadata> &info)
{
    switch (info.param) {
        case InvalidPausedSnapshotMetadata::UNSET:
            return "Unset";
        case InvalidPausedSnapshotMetadata::UNSPECIFIED:
            return "Unspecified";
        case InvalidPausedSnapshotMetadata::DELETING:
            return "Deleting";
        case InvalidPausedSnapshotMetadata::READY_WITHOUT_CHECKPOINT_ID:
            return "ReadyWithoutCheckpointID";
        case InvalidPausedSnapshotMetadata::OWNER_MISSING:
            return "OwnerMissing";
        case InvalidPausedSnapshotMetadata::OWNER_MISMATCH:
            return "OwnerMismatch";
    }
    return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(InvalidMetadata, SnapCtrlActorPausedSnapshotValidationTest,
                         Values(InvalidPausedSnapshotMetadata::UNSET,
                                InvalidPausedSnapshotMetadata::UNSPECIFIED,
                                InvalidPausedSnapshotMetadata::DELETING,
                                InvalidPausedSnapshotMetadata::READY_WITHOUT_CHECKPOINT_ID,
                                InvalidPausedSnapshotMetadata::OWNER_MISSING,
                                InvalidPausedSnapshotMetadata::OWNER_MISMATCH),
                         InvalidPausedSnapshotMetadataName);

class SnapCtrlActorPauseStateTest : public SnapCtrlActorPauseContextTest,
                                    public WithParamInterface<InstanceState> {
};

TEST_P(SnapCtrlActorPauseStateTest, PauseResumeRejectsTerminalStateBeforePrepare)
{
    UpdateInstanceInfo([state = GetParam()](resources::InstanceInfo &info) {
        info.mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    });
    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "must not prepare"));

    auto response = HandleSnapshot("pause-terminal-state", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

std::string TerminalStateName(const TestParamInfo<InstanceState> &info)
{
    return info.param == InstanceState::EXITING ? "Exiting" : "Fatal";
}

INSTANTIATE_TEST_SUITE_P(TerminalStates, SnapCtrlActorPauseStateTest,
                         Values(InstanceState::EXITING, InstanceState::FATAL), TerminalStateName);

enum class IdentityMutation {
    INSTANCE_ID,
    OWNER,
    INSTANCE_REQUEST_ID,
    VERSION,
    RUNTIME_ID,
    AGENT_ID,
    CONTAINER_ID,
    UNIT_ID,
    TENANT_ID,
    RUNTIME_ADDRESS,
};

class SnapCtrlActorPauseFenceTest : public SnapCtrlActorPauseContextTest,
                                    public WithParamInterface<IdentityMutation> {
};

TEST_P(SnapCtrlActorPauseFenceTest, PauseResumeRejectsCompletionAfterIdentityChanges)
{
    auto response = HandleSnapshot("pause-fenced", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    ASSERT_AWAIT_NO_SET_FOR(response, 50);

    UpdateInstanceInfo([mutation = GetParam()](resources::InstanceInfo &info) {
        switch (mutation) {
            case IdentityMutation::INSTANCE_ID:
                info.set_instanceid("replacement-instance");
                break;
            case IdentityMutation::OWNER:
                info.set_functionproxyid(OTHER_PROXY_ID);
                break;
            case IdentityMutation::INSTANCE_REQUEST_ID:
                info.set_requestid("replacement-request");
                break;
            case IdentityMutation::VERSION:
                info.set_version(INSTANCE_VERSION + 1);
                break;
            case IdentityMutation::RUNTIME_ID:
                info.set_runtimeid("replacement-runtime");
                break;
            case IdentityMutation::AGENT_ID:
                info.set_functionagentid("replacement-agent");
                break;
            case IdentityMutation::CONTAINER_ID:
                info.set_containerid("replacement-container");
                break;
            case IdentityMutation::UNIT_ID:
                info.set_unitid("replacement-unit");
                break;
            case IdentityMutation::TENANT_ID:
                info.set_tenantid("replacement-tenant");
                break;
            case IdentityMutation::RUNTIME_ADDRESS:
                info.set_runtimeaddress("replacement-address");
                break;
        }
    });
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
}

std::string IdentityMutationName(const TestParamInfo<IdentityMutation> &info)
{
    switch (info.param) {
        case IdentityMutation::INSTANCE_ID:
            return "InstanceID";
        case IdentityMutation::OWNER:
            return "Owner";
        case IdentityMutation::INSTANCE_REQUEST_ID:
            return "InstanceRequestID";
        case IdentityMutation::VERSION:
            return "Version";
        case IdentityMutation::RUNTIME_ID:
            return "RuntimeID";
        case IdentityMutation::AGENT_ID:
            return "AgentID";
        case IdentityMutation::CONTAINER_ID:
            return "ContainerID";
        case IdentityMutation::UNIT_ID:
            return "UnitID";
        case IdentityMutation::TENANT_ID:
            return "TenantID";
        case IdentityMutation::RUNTIME_ADDRESS:
            return "RuntimeAddress";
    }
    return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(IdentityFields, SnapCtrlActorPauseFenceTest,
                         Values(IdentityMutation::INSTANCE_ID, IdentityMutation::OWNER,
                                IdentityMutation::INSTANCE_REQUEST_ID, IdentityMutation::VERSION,
                                IdentityMutation::RUNTIME_ID, IdentityMutation::AGENT_ID,
                                IdentityMutation::CONTAINER_ID, IdentityMutation::UNIT_ID,
                                IdentityMutation::TENANT_ID, IdentityMutation::RUNTIME_ADDRESS),
                         IdentityMutationName);

TEST_F(SnapCtrlActorPauseContextTest, ProxyNeverReadsLocalArtifactOrCallsSnapshotStorage)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, ReleasePausedInstanceResources(testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &) {
            operations.emplace_back("resource-release");
            return litebus::Future<Status>(Status::OK());
        }));

    auto response = HandleSnapshot("pause-agent-owned-data", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(response);

    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(operations,
              (std::vector<std::string>{ "checkpoint", "release", "cas", "resource-release" }));
    ASSERT_NE(stateMachine_->pausedScheduleRequest, nullptr);
    const auto &snapshot = stateMachine_->pausedScheduleRequest->instance().snapshotinfo();
    EXPECT_EQ(snapshot.status(), resources::SNAPSHOT_READY);
    EXPECT_EQ(snapshot.storage(), "obs");
    EXPECT_EQ(snapshot.size(), 4096);
    EXPECT_EQ(snapshot.sha256(), "agent-persisted-pause-sha256");
}

TEST_F(SnapCtrlActorPauseContextTest, PauseSuccessWaitsForResourceViewReleaseAfterPausedCas)
{
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(nullptr);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Return(Status::OK()));
    auto resourceRelease = std::make_shared<litebus::Promise<Status>>();
    EXPECT_CALL(*instanceCtrl_, ReleasePausedInstanceResources(testing::_))
        .WillOnce(Return(resourceRelease->GetFuture()));

    auto response = HandleSnapshot("pause-resource-release-barrier", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_TRUE_FOR([this]() { return stateMachine_->pauseTransitions.load() == 1; }, 5000);
    ASSERT_AWAIT_NO_SET_FOR(response, 50);

    resourceRelease->SetValue(Status::OK());
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
}

TEST_F(SnapCtrlActorPauseContextTest, PausedCasSendsAgentPauseCommittedFinalize)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-committed-attempt", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(response);

    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    const auto &finalize = snapshotRuntimeProbe_->FinalizeRequests().front();
    EXPECT_EQ(finalize.operation(), ::messages::PAUSE_COMMITTED);
    EXPECT_EQ(finalize.tenantid(), "tenant-a");
    EXPECT_EQ(finalize.instanceid(), INSTANCE_ID);
    EXPECT_EQ(finalize.snapshotid(), snapshotRuntimeProbe_->PauseSnapshotID());
    EXPECT_EQ(finalize.attemptid(), "pause-committed-attempt");
    EXPECT_EQ(finalize.runtimeid(), "runtime-a");
    EXPECT_EQ(finalize.expectedsize(), 4096U);
    EXPECT_EQ(finalize.expectedsha256(), "agent-persisted-pause-sha256");
}

TEST_F(SnapCtrlActorPauseContextTest, CasConflictSendsOnlyExactPauseAbortedFinalize)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->transitionStatus = Status(StatusCode::SCHEDULE_CONFLICTED, "version conflict");
    stateMachine_->replaceGenerationBeforeResult = true;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Return(Status::OK()));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-aborted-attempt", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(response);

    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    const auto &finalize = snapshotRuntimeProbe_->FinalizeRequests().front();
    EXPECT_EQ(finalize.operation(), ::messages::PAUSE_ABORTED);
    EXPECT_EQ(finalize.tenantid(), "tenant-a");
    EXPECT_EQ(finalize.instanceid(), INSTANCE_ID);
    EXPECT_EQ(finalize.snapshotid(), snapshotRuntimeProbe_->PauseSnapshotID());
    EXPECT_EQ(finalize.attemptid(), "pause-aborted-attempt");
    EXPECT_EQ(finalize.runtimeid(), "runtime-a");
    EXPECT_EQ(finalize.expectedsize(), 4096U);
    EXPECT_EQ(finalize.expectedsha256(), "agent-persisted-pause-sha256");
}

TEST_F(SnapCtrlActorPauseContextTest, PauseResumeWithStableIdentityConsumesAgentReadyMetadata)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(
        testing::Eq(std::static_pointer_cast<InstanceStateMachine>(stateMachine_)), testing::_))
        .WillOnce(Invoke([](const std::shared_ptr<InstanceStateMachine> &machine,
                            const TransContext &context) {
            return machine->TransitionTo(context);
        }));

    auto response = HandleSnapshot("pause-stable", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    ASSERT_AWAIT_NO_SET_FOR(response, 50);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    core_service::SnapInfo result;
    ASSERT_TRUE(result.ParseFromString(response.Get().payload()));
    EXPECT_EQ(result.snapshotid(), "pause-stable");
    EXPECT_EQ(result.size(), 4096);
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 1);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseType(), common::PAUSE_RESUME);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseTtl(), 90'000);
    EXPECT_TRUE(snapshotRuntimeProbe_->PauseCheckpointDir().empty());
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotID(), "pause-stable");
    EXPECT_EQ(operations, (std::vector<std::string>{ "checkpoint", "release", "cas" }));
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 1);
    EXPECT_EQ(stateMachine_->transitionVersion, INSTANCE_VERSION);
    ASSERT_NE(stateMachine_->pausedScheduleRequest, nullptr);
    const auto &snapshotInfo = stateMachine_->pausedScheduleRequest->instance().snapshotinfo();
    EXPECT_EQ(snapshotInfo.checkpointid(), snapshotRuntimeProbe_->PauseSnapshotID());
    EXPECT_EQ(snapshotInfo.storage(), "obs");
    EXPECT_EQ(snapshotInfo.size(), 4096);
    EXPECT_EQ(snapshotInfo.sha256(), "agent-persisted-pause-sha256");
    EXPECT_EQ(snapshotInfo.ttlseconds(), 90'000);
    EXPECT_EQ(snapshotInfo.status(), resources::SNAPSHOT_READY);
    EXPECT_EQ(snapshotInfo.sourcenodeid(), OWNER_PROXY_ID);
    EXPECT_EQ(snapshotInfo.createtime(), "1700000000");
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

// Kills successful Pause completion that leaves the source reservation owned by
// the committed snapshot and consequently rejects the next Pause generation.
TEST_F(SnapCtrlActorPauseContextTest, SuccessfulPauseFinalizesSourceBeforeSecondPauseOnSameNode)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .Times(2)
        .WillRepeatedly(Return(Status::OK()));

    auto first = HandleSnapshot("pause-success-finalize-first", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(first);
    ASSERT_EQ(first.Get().code(), common::ERR_NONE) << first.Get().message();
    ASSERT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 1);
    const auto firstSnapshot = snapshotRuntimeProbe_->PauseSnapshotIDs().front();

    prepareClient_->Reset();
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_version(INSTANCE_VERSION + 2);
        info.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
        info.set_functionproxyid(OWNER_PROXY_ID);
        info.set_runtimeid("runtime-b");
        info.set_functionagentid("agent-b");
        info.set_containerid("container-b");
        info.set_unitid("unit-b");
        info.set_runtimeaddress("runtime-address-b");
        info.clear_snapshotinfo();
    });
    auto second = HandleSnapshot("pause-success-finalize-second", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 2; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(second);

    EXPECT_EQ(second.Get().code(), common::ERR_NONE) << second.Get().message();
    ASSERT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 2);
    EXPECT_NE(snapshotRuntimeProbe_->PauseSnapshotIDs().back(), firstSnapshot);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 2);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 2U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests()[0].operation(), ::messages::PAUSE_COMMITTED);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests()[1].operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, ReleaseFutureErrorRetriesSameSourceAndSnapshotIdentity)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    litebus::Future<Status> failedRelease;
    failedRelease.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    std::vector<std::string> releaseSnapshotIDs;
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations, &releaseSnapshotIDs, failedRelease](
                             const resources::InstanceInfo &info, const std::string &snapshotID) mutable {
            operations.emplace_back("release");
            EXPECT_EQ(info.runtimeid(), "runtime-a");
            releaseSnapshotIDs.emplace_back(snapshotID);
            return failedRelease;
        }))
        .WillOnce(Invoke([&operations, &releaseSnapshotIDs](
                             const resources::InstanceInfo &info, const std::string &snapshotID) {
            operations.emplace_back("release");
            EXPECT_EQ(info.runtimeid(), "runtime-a");
            releaseSnapshotIDs.emplace_back(snapshotID);
            return litebus::Future<Status>(Status::OK());
        }));

    auto response = HandleSnapshot("pause-release-future-error", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "release", "cas"));
    ASSERT_EQ(releaseSnapshotIDs.size(), 2);
    EXPECT_EQ(releaseSnapshotIDs[0], releaseSnapshotIDs[1]);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, PausedMetadataRecordsConfiguredObsBackendInsteadOfLocalArtifactStorage)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-obs-backend", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    ASSERT_EQ(response.Get().code(), common::ERR_NONE);
    ASSERT_NE(stateMachine_->pausedScheduleRequest, nullptr);
    EXPECT_EQ(stateMachine_->pausedScheduleRequest->instance().snapshotinfo().storage(), "obs");
}

TEST_F(SnapCtrlActorPauseContextTest, UnknownStorageRejectsSandboxLocalArtifactPathAsBackend)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    snapshotRuntimeProbe_->ConfigurePauseStorageBackend("/runtime/checkpoints/pause/checkpoint.img");
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .Times(AnyNumber())
        .WillRepeatedly(Return(Status::OK()));

    auto response = HandleSnapshot("pause-local-storage-backend", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseContextTest, ReleaseStatusFailureRetriesExactDeleteAndCommitsPaused)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status(StatusCode::FAILED, "release failed"));
        }))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release-retry");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-release-failure", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE) << response.Get().message();
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 1);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "release-retry"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, PausedCasConflictIsNotReportedAsSuccess)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->transitionStatus = Status(StatusCode::SCHEDULE_CONFLICTED, "version conflict");
    stateMachine_->replaceGenerationBeforeResult = true;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-cas-conflict", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 1);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "cas"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_ABORTED);
}

TEST_F(SnapCtrlActorPauseContextTest, CasFutureErrorRereadsExactCommittedPausedAsSuccess)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->commitPausedBeforeResult = true;
    stateMachine_->transitionFutureError = true;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-cas-committed-reread", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "cas"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, CasFutureErrorUsesAuthoritativePausedNPlusOneWhenLocalStillRunning)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->commitPausedBeforeResult = true;
    stateMachine_->keepLocalRunningAfterCommit = true;
    stateMachine_->transitionFutureError = true;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-cas-authoritative-committed", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(stateMachine_->syncCalls.load(), 1);
    EXPECT_EQ(stateMachine_->GetInstanceInfo().version(), INSTANCE_VERSION);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "cas"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, CasSyncFutureFailureRetriesWithoutDeletingCommittedFinal)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->commitPausedBeforeResult = true;
    stateMachine_->keepLocalRunningAfterCommit = true;
    stateMachine_->transitionFutureError = true;
    stateMachine_->syncFutureFailuresRemaining = 1;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-cas-sync-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(stateMachine_->syncCalls.load(), 2);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "cas"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, CasConflictWithSameFrozenLogicalSourceRetriesSameCas)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->transitionFailuresRemaining = 1;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_runtimeaddress("source-address-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-cas-running-reread", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_NONE) << response.Get().message();
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "cas", "cas"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_COMMITTED);
}

TEST_F(SnapCtrlActorPauseContextTest, CasFutureErrorAfterGenerationChangeOnlyDeletesExactAttemptArtifacts)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    stateMachine_->replaceGenerationBeforeResult = true;
    stateMachine_->transitionFutureError = true;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-cas-replaced-reread", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release", "cas"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_ABORTED);
}

TEST_F(SnapCtrlActorPauseContextTest, ReusableSnapshotCommitsReadyBeforeExactLocalCleanupAndKeepsSourceRunning)
{
    std::vector<std::string> operations;
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    snapshotRuntimeProbe_->ConfigureReusableSnapshotSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_function("akernel/rrt/latest");
    });
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    actor_->BindLocalSchedSrv(localSchedSrv);
    bool tunnelGateEntered = false;
    bool tunnelGateReleased = false;
    actor_->BindReusableSnapshotTunnelGate(
        [&tunnelGateEntered](const std::string &instanceID) {
            EXPECT_EQ(instanceID, INSTANCE_ID);
            tunnelGateEntered = true;
            return true;
        },
        [&tunnelGateReleased](const std::string &instanceID) {
            EXPECT_EQ(instanceID, INSTANCE_ID);
            tunnelGateReleased = true;
        });

    EXPECT_CALL(*localSchedSrv, BeginReusableSnapshot(testing::_))
        .WillOnce(Invoke([](const std::shared_ptr<::messages::BeginReusableSnapshotRequest> &request) {
            EXPECT_EQ(request->requestid(), "reusable-snapshot");
            EXPECT_EQ(request->tenantid(), "tenant-a");
            EXPECT_EQ(request->sourceinstanceid(), INSTANCE_ID);
            EXPECT_THAT(request->names(), ElementsAre("python-ready"));
            EXPECT_FALSE(request->requestfingerprint().empty());
            ::messages::BeginReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            response.set_snapshotid("snapshot-reusable-1");
            response.set_phase(::messages::REUSABLE_SNAPSHOT_PUBLISHING);
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, CommitReusableSnapshot(testing::_))
        .WillOnce(Invoke([&operations](const std::shared_ptr<::messages::CommitReusableSnapshotRequest> &request) {
            operations.emplace_back("commit");
            EXPECT_EQ(request->requestid(), "reusable-snapshot");
            EXPECT_EQ(request->tenantid(), "tenant-a");
            EXPECT_EQ(request->snapshotid(), "snapshot-reusable-1");
            EXPECT_EQ(request->sourceinstanceinfo().instanceid(), INSTANCE_ID);
            EXPECT_EQ(request->artifact().storagebackend(), "obs");
            EXPECT_EQ(request->artifact().objectkey(),
                      "reusable/v1/tenant/snapshot/checkpoint.img");
            ::messages::CommitReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            response.mutable_snapshotinfo()->set_snapshotid(request->snapshotid());
            response.mutable_snapshotinfo()->add_names("python-ready");
            response.set_version(INSTANCE_VERSION + 1);
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, FailReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ForceDeleteInstance(testing::_)).Times(0);

    auto response = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot,
                                   std::string("reusable-snapshot"), std::string(INSTANCE_ID),
                                   MakeSnapshotPayload(common::SNAPSHOT, "python-ready"));
    requests_.emplace_back(response);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    ASSERT_EQ(response.Get().code(), common::ERR_NONE) << response.Get().message();
    EXPECT_EQ(snapshotRuntimeProbe_->Calls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 1);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseType(), common::SNAPSHOT);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseTtl(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotID(), "snapshot-reusable-1");
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(),
              ::messages::REUSABLE_SNAPSHOT_COMMITTED);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "snapstarted", "commit", "finalize"));
    EXPECT_EQ(stateMachine_->GetInstanceInfo().instancestatus().code(),
              static_cast<int32_t>(InstanceState::RUNNING));
    EXPECT_TRUE(tunnelGateEntered);
    EXPECT_TRUE(tunnelGateReleased);
    core_service::SnapshotInfo publicInfo;
    ASSERT_TRUE(publicInfo.ParseFromString(response.Get().payload()));
    EXPECT_EQ(publicInfo.snapshotid(), "snapshot-reusable-1");
    EXPECT_THAT(publicInfo.names(), ElementsAre("python-ready"));
}

TEST_F(SnapCtrlActorPauseContextTest, ReusableSnapshotCommitFailureCleansExactArtifactBeforeRemovingPublishingRecord)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureReusableSnapshotSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_function("akernel/rrt/latest");
    });
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    actor_->BindLocalSchedSrv(localSchedSrv);

    EXPECT_CALL(*localSchedSrv, BeginReusableSnapshot(testing::_))
        .WillOnce(Invoke([](const std::shared_ptr<::messages::BeginReusableSnapshotRequest> &request) {
            ::messages::BeginReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            response.set_snapshotid("snapshot-reusable-failed");
            response.set_phase(::messages::REUSABLE_SNAPSHOT_PUBLISHING);
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, CommitReusableSnapshot(testing::_))
        .WillOnce(Invoke([&operations](const std::shared_ptr<::messages::CommitReusableSnapshotRequest> &request) {
            operations.emplace_back("commit");
            ::messages::CommitReusableSnapshotResponse response;
            response.set_code(common::ERR_INNER_SYSTEM_ERROR);
            response.set_requestid(request->requestid());
            response.set_message("injected commit failure");
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, FailReusableSnapshot(testing::_))
        .WillOnce(Invoke([&operations](const std::shared_ptr<::messages::FailReusableSnapshotRequest> &request) {
            operations.emplace_back("fail-record");
            EXPECT_EQ(request->snapshotid(), "snapshot-reusable-failed");
            EXPECT_FALSE(request->requestfingerprint().empty());
            ::messages::FailReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            return response;
        }));
    EXPECT_CALL(*instanceCtrl_, ForceDeleteInstance(testing::_)).Times(0);

    auto response = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot,
                                   std::string("reusable-snapshot-failure"), std::string(INSTANCE_ID),
                                   MakeSnapshotPayload(common::SNAPSHOT, "python-ready"));
    requests_.emplace_back(response);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(),
              ::messages::REUSABLE_SNAPSHOT_ABORTED);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "commit", "finalize", "fail-record"));
    EXPECT_EQ(stateMachine_->GetInstanceInfo().instancestatus().code(),
              static_cast<int32_t>(InstanceState::RUNNING));
}

TEST_F(SnapCtrlActorPauseContextTest, ReusablePublishFailureWithExactArtifactCleansBeforeFailingRecord)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureReusableSnapshotFailureWithArtifact(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_function("akernel/rrt/latest");
    });
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    actor_->BindLocalSchedSrv(localSchedSrv);

    EXPECT_CALL(*localSchedSrv, BeginReusableSnapshot(testing::_))
        .WillOnce(Invoke([](const std::shared_ptr<::messages::BeginReusableSnapshotRequest> &request) {
            ::messages::BeginReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            response.set_snapshotid("snapshot-reusable-publish-failed");
            response.set_phase(::messages::REUSABLE_SNAPSHOT_PUBLISHING);
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, CommitReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*localSchedSrv, FailReusableSnapshot(testing::_))
        .WillOnce(Invoke([&operations](const std::shared_ptr<::messages::FailReusableSnapshotRequest> &request) {
            operations.emplace_back("fail-record");
            EXPECT_EQ(request->snapshotid(), "snapshot-reusable-publish-failed");
            ::messages::FailReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            return response;
        }));
    EXPECT_CALL(*instanceCtrl_, ForceDeleteInstance(testing::_)).Times(0);

    auto response = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot,
                                   std::string("reusable-publish-failure"), std::string(INSTANCE_ID),
                                   MakeSnapshotPayload(common::SNAPSHOT, "python-ready"));
    requests_.emplace_back(response);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(),
              ::messages::REUSABLE_SNAPSHOT_ABORTED);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "finalize", "fail-record"));
    EXPECT_EQ(stateMachine_->GetInstanceInfo().instancestatus().code(),
              static_cast<int32_t>(InstanceState::RUNNING));
}

TEST_F(SnapCtrlActorPauseContextTest, ReusablePublishFailureWithoutExactArtifactPreservesPublishingRecord)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "publish identity unavailable", &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_function("akernel/rrt/latest");
    });
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    actor_->BindLocalSchedSrv(localSchedSrv);

    EXPECT_CALL(*localSchedSrv, BeginReusableSnapshot(testing::_))
        .WillOnce(Invoke([](const std::shared_ptr<::messages::BeginReusableSnapshotRequest> &request) {
            ::messages::BeginReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            response.set_snapshotid("snapshot-reusable-identity-unknown");
            response.set_phase(::messages::REUSABLE_SNAPSHOT_PUBLISHING);
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, CommitReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*localSchedSrv, FailReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ForceDeleteInstance(testing::_)).Times(0);

    auto response = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot,
                                   std::string("reusable-publish-identity-unknown"),
                                   std::string(INSTANCE_ID),
                                   MakeSnapshotPayload(common::SNAPSHOT, "python-ready"));
    requests_.emplace_back(response);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_TRUE(snapshotRuntimeProbe_->FinalizeRequests().empty());
    EXPECT_THAT(operations, ElementsAre("checkpoint"));
    EXPECT_EQ(stateMachine_->GetInstanceInfo().instancestatus().code(),
              static_cast<int32_t>(InstanceState::RUNNING));
}

TEST_F(SnapCtrlActorPauseContextTest, ReusableAgentResultUnknownPreservesPublishingRecord)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "unused", &operations);
    snapshotRuntimeProbe_->ConfigurePauseFutureFailures(1);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_function("akernel/rrt/latest");
    });
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    actor_->BindLocalSchedSrv(localSchedSrv);

    EXPECT_CALL(*localSchedSrv, BeginReusableSnapshot(testing::_))
        .WillOnce(Invoke([](const std::shared_ptr<::messages::BeginReusableSnapshotRequest> &request) {
            ::messages::BeginReusableSnapshotResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(request->requestid());
            response.set_snapshotid("snapshot-reusable-result-unknown");
            response.set_phase(::messages::REUSABLE_SNAPSHOT_PUBLISHING);
            return response;
        }));
    EXPECT_CALL(*localSchedSrv, CommitReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*localSchedSrv, FailReusableSnapshot(testing::_)).Times(0);

    auto response = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot,
                                   std::string("reusable-agent-result-unknown"),
                                   std::string(INSTANCE_ID),
                                   MakeSnapshotPayload(common::SNAPSHOT, "python-ready"));
    requests_.emplace_back(response);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_TRUE(snapshotRuntimeProbe_->FinalizeRequests().empty());
    EXPECT_THAT(operations, ElementsAre("checkpoint"));
}

TEST_F(SnapCtrlActorPauseContextTest, ActiveReverseTunnelRejectsReusableSnapshotBeforePublishingRecord)
{
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
    });
    auto localSchedSrv = std::make_shared<MockLocalSchedSrv>();
    actor_->BindLocalSchedSrv(localSchedSrv);
    actor_->BindReusableSnapshotTunnelGate(
        [](const std::string &) { return false; }, [](const std::string &) {});

    EXPECT_CALL(*localSchedSrv, BeginReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*localSchedSrv, CommitReusableSnapshot(testing::_)).Times(0);
    EXPECT_CALL(*localSchedSrv, FailReusableSnapshot(testing::_)).Times(0);

    auto response = litebus::Async(actor_->GetAID(), &SnapCtrlActor::HandleSnapshot,
                                   std::string("snapshot-active-tunnel"), std::string(INSTANCE_ID),
                                   MakeSnapshotPayload(common::SNAPSHOT, "python-ready"));
    requests_.emplace_back(response);

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(response.Get().message(), HasSubstr("tunnel"));
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
}

class SnapCtrlActorPauseRescueTest : public SnapCtrlActorPauseContextTest {
};

class SnapCtrlActorPauseDeleteTest : public SnapCtrlActorPauseRescueTest {
};

TEST_F(SnapCtrlActorPauseRescueTest, RunningCheckpointFailureNotifiesRuntimeBeforeRecoveringGate)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "checkpoint primary failure", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &) {
            operations.emplace_back("recover");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-running-rescue", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), common::ERR_INNER_COMMUNICATION);
    EXPECT_EQ(response.Get().message(), "checkpoint primary failure");
    EXPECT_EQ(operations, (std::vector<std::string>{ "checkpoint", "snapstarted", "recover" }));
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseRescueTest, RunningRescueGetClientFutureFailureRetriesSameContext)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "running original error", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-running-client-future-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> failedClient;
    failedClient.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    Mock::VerifyAndClearExpectations(clientManager_.get());
    EXPECT_CALL(*clientManager_, GetControlInterfacePosixClient(INSTANCE_ID))
        .WillOnce(Return(failedClient))
        .WillOnce(Return(prepareClient_));
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    ASSERT_FALSE(response.IsError());
    EXPECT_EQ(response.Get().message(), "running original error");
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 1U);
}

TEST_F(SnapCtrlActorPauseRescueTest, DeleteDuringPendingPrepareCancelsBeforeRunningRescue)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "running delete original error", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    prepareClient_->ConfigureSnapStartedFutureFailures(1);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-running-started-future-retry", common::PAUSE_RESUME);
    FlushActor();
    auto waiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                 std::string(INSTANCE_ID));
    ASSERT_AWAIT_READY_FOR(waiter, 100);
    ASSERT_AWAIT_READY_FOR(response, 100);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);

    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "late prepared"));
    FlushActor();
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 0);
    EXPECT_TRUE(operations.empty());
    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), waiter.Get().generation);
    ASSERT_AWAIT_READY(finished);
    EXPECT_TRUE(finished.Get().IsOk());
}

TEST_F(SnapCtrlActorPauseRescueTest, RunningRescueRecoverGateFutureFailureRetriesOriginalError)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "running recover future original", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    litebus::Future<Status> failedRecover;
    failedRecover.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .WillOnce(Return(failedRecover))
        .WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-running-recover-future-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    ASSERT_FALSE(response.IsError());
    EXPECT_EQ(response.Get().message(), "running recover future original");
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
}

TEST_F(SnapCtrlActorPauseRescueTest, RunningRescueRecoverGateStatusFailureRetriesOriginalError)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "running recover status original", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .WillOnce(Return(Status(StatusCode::FAILED, "recover status retry")))
        .WillOnce(Return(Status::OK()));

    auto response = HandleSnapshot("pause-running-recover-status-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().message(), "running recover status original");
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 1);
}

TEST_F(SnapCtrlActorPauseRescueTest, SnapStartedFailureLeavesRunningSourceGated)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "checkpoint primary failure", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_INNER_COMMUNICATION, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-running-snapstarted-failure", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(response.Get().message(), HasSubstr("SnapStarted"));
    EXPECT_EQ(operations, (std::vector<std::string>{ "checkpoint", "snapstarted" }));
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

// Source-runtime restore compensation was intentionally removed: leave-running
// checkpoints retry the exact source delete or PAUSED CAS instead of deploying a
// replacement source sandbox. The retry semantics are covered above.

TEST_F(SnapCtrlActorPauseRescueTest, UnknownPhysicalFactRetriesSameSnapshotIdentityUntilRunningRescue)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailureSequence(
        { runtime::v1::SANDBOX_STATE_UNKNOWN, runtime::v1::SANDBOX_STATE_RUNNING },
        "checkpoint result remains unknown", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_))
        .WillOnce(Invoke([&operations](const resources::InstanceInfo &) {
            operations.emplace_back("recover");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-unknown-retry", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().message(), "checkpoint result remains unknown");
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 2);
    ASSERT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs().size(), 2);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseSnapshotIDs()[0], snapshotRuntimeProbe_->PauseSnapshotIDs()[1]);
    EXPECT_EQ(operations,
              (std::vector<std::string>{ "checkpoint", "checkpoint", "snapstarted", "recover" }));
}

TEST_F(SnapCtrlActorPauseRescueTest, ExitedWithoutTrustedArtifactRemainsFailClosed)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_EXITED,
                                                 "checkpoint source exited", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-exited", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(response.Get().message(), "checkpoint source exited");
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 0);
    EXPECT_EQ(operations, (std::vector<std::string>{ "checkpoint" }));
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseRescueTest, LateAgentReadyAfterRuntimeChangeAbortsExactAttempt)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    snapshotRuntimeProbe_->HoldPauseResponse();
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_)).Times(0);

    auto response = HandleSnapshot("pause-late-agent-runtime", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_TRUE_FOR([&operations]() { return operations == std::vector<std::string>{ "checkpoint" }; },
                          5000);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("replacement-runtime"); });
    snapshotRuntimeProbe_->CompletePauseResponse(snapshotRuntimeProbe_->AgentPersistedPauseResponse());

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_ABORTED);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseRescueTest, LateAgentReadyAfterAgentChangeAbortsExactAttempt)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    snapshotRuntimeProbe_->HoldPauseResponse();
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_functionagentid("agent-a"); });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_)).Times(0);

    auto response = HandleSnapshot("pause-late-agent-agent", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_TRUE_FOR([&operations]() { return operations == std::vector<std::string>{ "checkpoint" }; },
                          5000);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_functionagentid("replacement-agent"); });
    snapshotRuntimeProbe_->CompletePauseResponse(snapshotRuntimeProbe_->AgentPersistedPauseResponse());

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_ABORTED);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseRescueTest, LateAgentReadyAfterContainerChangeAbortsExactAttempt)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    snapshotRuntimeProbe_->HoldPauseResponse();
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_containerid("container-a"); });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_)).Times(0);

    auto response = HandleSnapshot("pause-late-agent-container", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_TRUE_FOR([&operations]() { return operations == std::vector<std::string>{ "checkpoint" }; },
                          5000);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_containerid("replacement-container"); });
    snapshotRuntimeProbe_->CompletePauseResponse(snapshotRuntimeProbe_->AgentPersistedPauseResponse());

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_ABORTED);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseRescueTest, LateReleaseAfterAgentIdentityChangeDoesNotCommitPaused)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
        info.set_functionagentid("agent-a");
        info.set_containerid("container-a");
        info.set_unitid("unit-a");
        info.set_runtimeaddress("runtime-address-a");
    });
    auto release = std::make_shared<litebus::Promise<Status>>();
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_))
        .WillOnce(Invoke([release, &operations](const resources::InstanceInfo &, const std::string &) {
            operations.emplace_back("release");
            return release->GetFuture();
        }));
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-late-release-identity", common::PAUSE_RESUME);
    ASSERT_AWAIT_TRUE_FOR([this]() { return prepareClient_->PrepareCalls() == 1; }, 5000);
    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "prepared"));
    ASSERT_AWAIT_TRUE_FOR([&operations]() {
        return !operations.empty() && operations.back() == "release";
    }, 5000);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_functionagentid("replacement-agent"); });
    release->SetValue(Status::OK());

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_THAT(operations, ElementsAre("checkpoint", "release"));
    ASSERT_EQ(snapshotRuntimeProbe_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(snapshotRuntimeProbe_->FinalizeRequests().front().operation(), ::messages::PAUSE_ABORTED);
    EXPECT_EQ(stateMachine_->pauseTransitions.load(), 0);
}

TEST_F(SnapCtrlActorPauseDeleteTest, DeleteDuringPendingPreparePreventsCheckpointArtifactCreation)
{
    std::vector<std::string> operations;
    stateMachine_->operations = &operations;
    snapshotRuntimeProbe_->ConfigureAgentPersistedPauseSuccess(&operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) {
        info.set_tenantid("tenant-a");
        info.set_runtimeid("runtime-a");
    });
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(testing::_, testing::_)).Times(0);

    auto response = HandleSnapshot("pause-success-before-delete", common::PAUSE_RESUME);
    FlushActor();
    auto waiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                 std::string(INSTANCE_ID));
    ASSERT_AWAIT_READY_FOR(waiter, 100);
    ASSERT_AWAIT_READY_FOR(response, 100);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);

    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "late prepared"));
    FlushActor();
    EXPECT_TRUE(operations.empty());
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
    EXPECT_TRUE(snapshotRuntimeProbe_->FinalizeRequests().empty());
    EXPECT_EQ(stateMachine_->pausedScheduleRequest, nullptr);
    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), waiter.Get().generation);
    ASSERT_AWAIT_READY(finished);
    EXPECT_TRUE(finished.Get().IsOk());
}

TEST_F(SnapCtrlActorPauseDeleteTest, DeleteDuringPendingPreparePreventsRunningSourceRescue)
{
    std::vector<std::string> operations;
    snapshotRuntimeProbe_->ConfigurePauseFailure(runtime::v1::SANDBOX_STATE_RUNNING,
                                                 "checkpoint primary failure", &operations);
    prepareClient_->ConfigureSnapStarted(common::ERR_NONE, &operations);
    UpdateInstanceInfo([](resources::InstanceInfo &info) { info.set_runtimeid("runtime-a"); });
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(testing::_)).Times(0);

    auto response = HandleSnapshot("pause-running-delete-waiter", common::PAUSE_RESUME);
    FlushActor();
    auto waiter = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                 std::string(INSTANCE_ID));
    ASSERT_AWAIT_READY_FOR(waiter, 100);
    ASSERT_AWAIT_READY_FOR(response, 100);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);

    prepareClient_->Complete(MakePrepareResponse(common::ERR_NONE, "late prepared"));
    FlushActor();
    EXPECT_TRUE(operations.empty());
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);
    EXPECT_EQ(prepareClient_->SnapStartedCalls(), 0);
    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), waiter.Get().generation);
    ASSERT_AWAIT_READY(finished);
    EXPECT_TRUE(finished.Get().IsOk());
}

TEST_F(SnapCtrlActorPauseDeleteTest, AuthorizedDeletePreparationWithoutContextBlocksPauseUntilFinished)
{
    EXPECT_CALL(*instanceCtrl_, RecoverPauseGate(testing::_)).WillOnce(Return(Status::OK()));
    auto preparation = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                      std::string(INSTANCE_ID));
    ASSERT_AWAIT_READY(preparation);
    EXPECT_NE(preparation.Get().generation, 0U);

    auto blocked = HandleSnapshot("pause-during-authorized-delete", common::PAUSE_RESUME);

    ASSERT_AWAIT_READY(blocked);
    EXPECT_EQ(blocked.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);
    EXPECT_EQ(snapshotRuntimeProbe_->PauseCalls(), 0);

    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), preparation.Get().generation);
    ASSERT_AWAIT_READY(finished);
    EXPECT_TRUE(finished.Get().IsOk());

    prepareClient_->Complete(MakePrepareResponse(common::ERR_INNER_COMMUNICATION, "prepare failed after release"));
    auto retry = HandleSnapshot("pause-after-delete-intent-release", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(retry);
    EXPECT_NE(retry.Get().code(), common::ERR_NONE);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 1);
}

TEST_F(SnapCtrlActorPauseDeleteTest, AuthorizedDeletePreparationIsGenerationFencedAndShared)
{
    auto first = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                std::string(INSTANCE_ID));
    auto duplicate = litebus::Async(actor_->GetAID(), &SnapCtrlActor::PrepareForAuthorizedDelete,
                                    std::string(INSTANCE_ID));
    ASSERT_AWAIT_READY(first);
    ASSERT_AWAIT_READY(duplicate);
    EXPECT_NE(first.Get().generation, 0U);
    EXPECT_EQ(duplicate.Get().generation, first.Get().generation);

    auto staleFinish = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                      std::string(INSTANCE_ID), first.Get().generation + 1);
    ASSERT_AWAIT_READY(staleFinish);
    EXPECT_EQ(staleFinish.Get().StatusCode(), StatusCode::SCHEDULE_CONFLICTED);

    auto stillBlocked = HandleSnapshot("pause-after-stale-delete-finish", common::PAUSE_RESUME);
    ASSERT_AWAIT_READY(stillBlocked);
    EXPECT_EQ(stillBlocked.Get().code(), common::ERR_STATE_MACHINE_ERROR);
    EXPECT_EQ(prepareClient_->PrepareCalls(), 0);

    auto finished = litebus::Async(actor_->GetAID(), &SnapCtrlActor::FinishAuthorizedDelete,
                                   std::string(INSTANCE_ID), first.Get().generation);
    ASSERT_AWAIT_READY(finished);
    EXPECT_TRUE(finished.Get().IsOk());
}

TEST(SnapCtrlResumeTest, TrustedResumeBoundaryUsesOnlyProcessLocalIdentityOverload)
{
    auto actor = std::make_shared<SnapCtrlActor>("trusted-resume-snap-ctrl", "target-node");
    litebus::Spawn(actor);
    auto instanceCtrl = std::make_shared<MockInstanceCtrl>();
    actor->BindInstanceCtrl(instanceCtrl);

    auto request = std::make_shared<messages::ScheduleRequest>();
    request->set_requestid("target-attempt");
    auto *instance = request->mutable_instance();
    instance->set_instanceid("logical-instance");
    instance->set_requestid("logical-generation");
    instance->set_tenantid("tenant-a");
    instance->set_functionagentid("target-agent-a");
    instance->set_version(12);
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
    auto *snapshot = instance->mutable_snapshotinfo();
    snapshot->set_checkpointid("snapshot-a");
    snapshot->set_status(resources::SNAPSHOT_READY);
    snapshot->set_storage("obs");
    snapshot->set_size(sizeof(ResumeBoundarySnapshotStorage::PAYLOAD) - 1);
    snapshot->set_sha256(resume_identity::Sha256Hex(ResumeBoundarySnapshotStorage::PAYLOAD));
    snapshot->set_createtime("1786672800");
    snapshot->set_ttlseconds(90000);
    const auto identity = std::make_shared<resume_identity::TrustedResumeIdentity>(
        resume_identity::TrustedResumeIdentity::FromSchedule(*request));
    auto authoritative = request->instance();
    authoritative.set_functionproxyid(INSTANCE_MANAGER_OWNER);
    authoritative.clear_functionagentid();
    auto resumeStateMachine = MakeInstanceStateMachine(authoritative);
    auto resumeView = std::make_shared<MockInstanceControlView>("target-node");
    EXPECT_CALL(*resumeView, GetInstance("logical-instance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(resumeStateMachine));
    actor->BindInstanceControlView(resumeView);

    litebus::Promise<messages::DeployInstanceResponse> pending;
    std::atomic<int> deployCalls{0};
    EXPECT_CALL(*instanceCtrl,
                DeploySnapStartInstance(
                    testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .WillOnce(Invoke([&](const std::shared_ptr<messages::ScheduleRequest> &received,
                             const resume_identity::TrustedResumeIdentity &receivedIdentity) {
            deployCalls.fetch_add(1);
            EXPECT_EQ(received->instance().instanceid(), "logical-instance");
            EXPECT_EQ(receivedIdentity.logicalInstanceID, "logical-instance");
            EXPECT_EQ(receivedIdentity.logicalRequestID, "logical-generation");
            EXPECT_EQ(receivedIdentity.targetAttemptID, "target-attempt");
            EXPECT_FALSE(resume_identity::HasReservedExtension(request->instance().scheduleoption().extension()));
            return pending.GetFuture();
        }));
    EXPECT_CALL(*instanceCtrl, DeploySnapStartInstance(request)).Times(0);

    auto response = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    TransitionResult transition;
    transition.preState = InstanceState::PAUSED;
    actor->SnapStart(response, request, schedule_decision::ScheduleResult{}, transition, identity);

    ASSERT_AWAIT_TRUE_FOR([&]() { return deployCalls.load() == 1; }, 5000);
    litebus::Terminate(actor->GetAID());
    litebus::Await(actor);
}

std::shared_ptr<messages::ScheduleRequest> MakeResumeAttemptRegistryRequest(
    const std::string &attemptID, const std::string &targetAgentID = "target-agent")
{
    auto request = std::make_shared<messages::ScheduleRequest>();
    request->set_requestid(attemptID);
    auto *instance = request->mutable_instance();
    instance->set_instanceid("logical-instance");
    instance->set_requestid("logical-generation");
    instance->set_tenantid("tenant-a");
    instance->set_functionagentid(targetAgentID);
    instance->set_version(12);
    instance->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
    auto *snapshot = instance->mutable_snapshotinfo();
    snapshot->set_checkpointid("snapshot-a");
    snapshot->set_status(resources::SNAPSHOT_READY);
    snapshot->set_storage("obs");
    snapshot->set_size(4096);
    snapshot->set_sha256("0123456789abcdef");
    snapshot->set_ttlseconds(90000);
    snapshot->set_createtime("1786633200");
    return request;
}

TEST(ResumeAttemptRegistryTest, CoalescesSameAttemptAndSeparatesDifferentAttempts)
{
    ResumeAttemptRegistry registry;
    auto firstRequest = MakeResumeAttemptRegistryRequest("attempt-a");
    auto firstIdentity = resume_identity::TrustedResumeIdentity::FromSchedule(*firstRequest);
    auto paused = firstRequest->instance();
    paused.set_functionproxyid(INSTANCE_MANAGER_OWNER);
    paused.clear_functionagentid();
    auto firstCompletion = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    auto replayCompletion = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();

    const auto inserted = registry.Register(firstIdentity, paused, firstRequest, firstCompletion);
    const auto replay = registry.Register(firstIdentity, paused, firstRequest, replayCompletion);
    auto secondRequest = MakeResumeAttemptRegistryRequest("attempt-b");
    const auto second = registry.Register(
        resume_identity::TrustedResumeIdentity::FromSchedule(*secondRequest), paused, secondRequest,
        std::make_shared<litebus::Promise<messages::ScheduleResponse>>());

    EXPECT_EQ(inserted.result, ResumeAttemptRegistration::Result::INSERTED);
    EXPECT_EQ(replay.result, ResumeAttemptRegistration::Result::COALESCED);
    ASSERT_EQ(inserted.context, replay.context);
    EXPECT_EQ(inserted.context->completions.size(), 2U);
    EXPECT_EQ(second.result, ResumeAttemptRegistration::Result::INSERTED);
    EXPECT_NE(second.context, inserted.context);
    EXPECT_TRUE(registry.Contains(inserted.context));
    EXPECT_TRUE(registry.Contains(second.context));

    auto conflictingRequest = MakeResumeAttemptRegistryRequest("attempt-a", "other-agent");
    const auto conflict = registry.Register(
        resume_identity::TrustedResumeIdentity::FromSchedule(*conflictingRequest), paused,
        conflictingRequest, std::make_shared<litebus::Promise<messages::ScheduleResponse>>());
    EXPECT_EQ(conflict.result, ResumeAttemptRegistration::Result::CONFLICT);
    EXPECT_EQ(inserted.context->completions.size(), 2U);
}

TEST(ResumeAttemptRegistryTest, WinnerRequiresExactRunningPhysicalIdentity)
{
    ResumeAttemptRegistry registry;
    auto request = MakeResumeAttemptRegistryRequest("attempt-winner");
    auto identity = resume_identity::TrustedResumeIdentity::FromSchedule(*request);
    auto paused = request->instance();
    paused.set_functionproxyid(INSTANCE_MANAGER_OWNER);
    paused.clear_functionagentid();
    auto registration = registry.Register(
        identity, paused, request,
        std::make_shared<litebus::Promise<messages::ScheduleResponse>>());
    ASSERT_EQ(registration.result, ResumeAttemptRegistration::Result::INSERTED);
    auto &candidate = registration.context->candidate;
    candidate.CopyFrom(request->instance());
    candidate.set_functionproxyid("target-proxy");
    candidate.set_functionagentid("target-agent");
    candidate.set_runtimeid("runtime-attempt-winner");
    candidate.set_runtimeaddress("10.0.0.8:9000");
    candidate.set_containerid("sandbox-attempt-winner");
    candidate.set_containerip("10.0.0.9");
    candidate.set_unitid("target-unit");
    candidate.set_proxygrpcaddress("10.0.0.8:10000");
    (*candidate.mutable_extensions())["portForward"] = "tcp:42001:8080";

    auto authoritative = candidate;
    authoritative.set_version(identity.expectedVersion + 1);
    authoritative.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    EXPECT_TRUE(registry.IsExactWinner(registration.context, authoritative));

    authoritative.set_runtimeid("different-runtime");
    EXPECT_FALSE(registry.IsExactWinner(registration.context, authoritative));
    authoritative.set_runtimeid(candidate.runtimeid());
    (*authoritative.mutable_extensions())["portForward"] = "tcp:42002:8080";
    EXPECT_FALSE(registry.IsExactWinner(registration.context, authoritative));
}

class SnapCtrlResumeCommitTest : public Test {
public:
    void SetUp() override
    {
        resources::InstanceInfo paused = MakeInstanceInfo(InstanceState::PAUSED, INSTANCE_MANAGER_OWNER);
        paused.set_tenantid("tenant-a");
        auto *snapshot = paused.mutable_snapshotinfo();
        snapshot->set_checkpointid("snapshot-a");
        snapshot->set_status(resources::SNAPSHOT_READY);
        snapshot->set_storage("obs");
        snapshot->set_size(sizeof(ResumeBoundarySnapshotStorage::PAYLOAD) - 1);
        snapshot->set_sha256(resume_identity::Sha256Hex(ResumeBoundarySnapshotStorage::PAYLOAD));
        snapshot->set_createtime("1786672800");
        snapshot->set_ttlseconds(90000);
        stateMachine_ = MakeInstanceStateMachine(paused);
        instanceControlView_ = std::make_shared<MockInstanceControlView>(OWNER_PROXY_ID);
        EXPECT_CALL(*instanceControlView_, GetInstance(INSTANCE_ID))
            .Times(AnyNumber())
            .WillRepeatedly(Return(stateMachine_));

        request_ = std::make_shared<messages::ScheduleRequest>();
        request_->set_requestid("target-attempt-a");
        request_->mutable_instance()->CopyFrom(paused);
        request_->mutable_instance()->set_functionproxyid(OWNER_PROXY_ID);
        request_->mutable_instance()->set_functionagentid("target-agent-a");
        request_->mutable_instance()->set_unitid("target-unit-a");
        identity_ = std::make_shared<resume_identity::TrustedResumeIdentity>(
            resume_identity::TrustedResumeIdentity::FromSchedule(*request_));

        operations_.emplace_back("restore");
        client_ = std::make_shared<ControllablePrepareSnapClient>();
        client_->ConfigureSnapStarted(common::ERR_NONE, &operations_);
        instanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        ON_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).WillByDefault(Return(Status::OK()));
        ON_CALL(*instanceCtrl_, ReleasePausedInstanceResources(_)).WillByDefault(Return(Status::OK()));
        ON_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillByDefault(Return(client_));
        ON_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _))
            .WillByDefault(Invoke([this](const std::string &, uint32_t, const std::string &, StatusCode) {
                operations_.emplace_back("heartbeat");
            }));
        storage_ = std::make_shared<ResumeBoundarySnapshotStorage>(&operations_, INSTANCE_VERSION - 1);
        agentMgr_ = std::make_shared<SnapshotRuntimeProbe>();
        agentMgr_->SetFinalizeResponse(
            static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE), true, false, true);
        actor_ = std::make_shared<SnapCtrlActor>("resume-commit-test", OWNER_PROXY_ID);
        actor_->BindInstanceControlView(instanceControlView_);
        actor_->BindInstanceCtrl(instanceCtrl_);
        actor_->BindFunctionAgentMgr(agentMgr_);
        litebus::Spawn(actor_);
    }

    void TearDown() override
    {
        if (actor_ != nullptr) {
            litebus::Terminate(actor_->GetAID());
            litebus::Await(actor_);
        }
    }

protected:
    litebus::Future<messages::ScheduleResponse> StartResume()
    {
        response_ = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
        TransitionResult paused;
        paused.preState = InstanceState::PAUSED;
        paused.previousInfo = stateMachine_->GetInstanceInfo();
        paused.version = INSTANCE_VERSION;
        actor_->SnapStart(response_, request_, schedule_decision::ScheduleResult{}, paused, identity_);
        return response_->GetFuture();
    }

    litebus::Future<messages::ScheduleResponse> BeginResume()
    {
        return BeginResumeWithMappings(R"(["http:40001:8080"])");
    }

    litebus::Future<messages::ScheduleResponse> BeginResumeWithMappings(std::string portMappings)
    {
        EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
            testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
            .WillOnce(Invoke([this, portMappings = std::move(portMappings)](
                                 const std::shared_ptr<messages::ScheduleRequest> &received,
                                 const resume_identity::TrustedResumeIdentity &) {
                EXPECT_EQ(received->instance().instanceid(), INSTANCE_ID);
                EXPECT_EQ(received->instance().requestid(), INSTANCE_REQUEST_ID);
                operations_.emplace_back("deploy");
                messages::DeployInstanceResponse response;
                response.set_code(common::ERR_NONE);
                response.set_runtimeid(RuntimeID());
                response.set_address("target-address-a");
                response.set_containerid("target-container-a");
                response.set_containerip("target-container-ip-a");
                response.set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
                response.set_timeinfo("target-start-a");
                response.set_portmappings(portMappings);
                return litebus::Future<messages::DeployInstanceResponse>(response);
            }));
        return StartResume();
    }

    void SeedStaleSourcePortForward()
    {
        constexpr char staleMappings[] = R"(["http:39001:9090","tcp:39022:2222"])";
        auto paused = stateMachine_->GetInstanceInfo();
        (*paused.mutable_extensions())["portForward"] = staleMappings;
        stateMachine_->UpdateInstanceInfo(paused);
        (*request_->mutable_instance()->mutable_extensions())["portForward"] = staleMappings;
        request_->mutable_instance()->set_proxygrpcaddress("10.0.0.9:19000");
    }

    std::string RuntimeID() const
    {
        return resume_identity::RuntimeID(INSTANCE_ID, identity_->targetAttemptID);
    }

    resources::InstanceInfo ExactWinner(const TransContext &context) const
    {
        auto winner = context.scheduleReq->instance();
        winner.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
        winner.set_version(INSTANCE_VERSION + 1);
        *winner.mutable_snapshotinfo() = identity_->snapshot;
        return winner;
    }

    resources::InstanceInfo RetainedSnapshotWinner(const TransContext &context) const
    {
        auto winner = context.scheduleReq->instance();
        winner.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
        winner.set_version(INSTANCE_VERSION + 1);
        *winner.mutable_snapshotinfo() = identity_->snapshot;
        return winner;
    }

    std::shared_ptr<PauseStateMachineProbe> stateMachine_;
    std::shared_ptr<MockInstanceControlView> instanceControlView_;
    std::shared_ptr<MockInstanceCtrl> instanceCtrl_;
    std::shared_ptr<ControllablePrepareSnapClient> client_;
    std::shared_ptr<ResumeBoundarySnapshotStorage> storage_;
    std::shared_ptr<SnapshotRuntimeProbe> agentMgr_;
    std::shared_ptr<SnapCtrlActor> actor_;
    std::shared_ptr<messages::ScheduleRequest> request_;
    std::shared_ptr<resume_identity::TrustedResumeIdentity> identity_;
    std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> response_;
    std::vector<std::string> operations_;
};

TEST_F(SnapCtrlResumeCommitTest, ProxyRestartReplaysResumeCommittedFromRunningRetainedSnapshot)
{
    auto winner = request_->instance();
    winner.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    winner.set_version(INSTANCE_VERSION + 1);
    winner.set_runtimeid(RuntimeID());
    winner.set_runtimeaddress("target-address-a");
    winner.set_functionproxyid(OWNER_PROXY_ID);
    winner.set_functionagentid("target-agent-a");
    winner.set_containerid("target-container-a");
    winner.set_unitid("target-unit-a");
    *winner.mutable_snapshotinfo() = identity_->snapshot;
    stateMachine_->UpdateInstanceInfo(winner);
    agentMgr_->SetFinalizeResponse(static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &,
                                const TransContext &context) {
            EXPECT_EQ(context.version, INSTANCE_VERSION + 1);
            EXPECT_FALSE(context.scheduleReq->instance().has_snapshotinfo());
            auto cleared = context.scheduleReq->instance();
            cleared.set_version(INSTANCE_VERSION + 2);
            stateMachine_->UpdateInstanceInfo(cleared);
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::RUNNING, {}, {}, INSTANCE_VERSION + 2, Status::OK() });
        }));

    litebus::Async(actor_->GetAID(), &SnapCtrlActor::ReplayCommittedResumeFinalize,
                   request_, winner, *identity_);

    ASSERT_AWAIT_TRUE_FOR([this]() { return agentMgr_->FinalizeRequests().size() == 1U; }, 5000);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().snapshotid(), identity_->snapshot.checkpointid());
    ASSERT_AWAIT_TRUE_FOR([this]() { return !stateMachine_->GetInstanceInfo().has_snapshotinfo(); }, 5000);
}

TEST_F(SnapCtrlResumeCommitTest, LateReplayFinalizeCannotClearNextPauseSnapshot)
{
    auto winner = request_->instance();
    winner.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    winner.set_version(INSTANCE_VERSION + 1);
    winner.set_runtimeid(RuntimeID());
    winner.set_runtimeaddress("target-address-a");
    winner.set_functionproxyid(OWNER_PROXY_ID);
    winner.set_functionagentid("target-agent-a");
    winner.set_containerid("target-container-a");
    winner.set_unitid("target-unit-a");
    *winner.mutable_snapshotinfo() = identity_->snapshot;
    stateMachine_->UpdateInstanceInfo(winner);
    agentMgr_->HoldFinalizeResponse();
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _)).Times(0);

    litebus::Async(actor_->GetAID(), &SnapCtrlActor::ReplayCommittedResumeFinalize,
                   request_, winner, *identity_);
    ASSERT_AWAIT_TRUE_FOR([this]() { return agentMgr_->FinalizeRequests().size() == 1U; }, 5000);
    auto nextPause = winner;
    nextPause.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
    nextPause.set_version(INSTANCE_VERSION + 2);
    nextPause.mutable_snapshotinfo()->set_checkpointid("snapshot-next-pause-after-restart");
    nextPause.mutable_snapshotinfo()->set_sha256("next-pause-after-restart-sha256");
    stateMachine_->UpdateInstanceInfo(nextPause);

    agentMgr_->CompleteFinalizeResponse(static_cast<int32_t>(StatusCode::SUCCESS), true, true);

    ASSERT_AWAIT_TRUE_FOR([this]() {
        return stateMachine_->GetInstanceInfo().snapshotinfo().checkpointid()
            == "snapshot-next-pause-after-restart";
    }, 5000);
}

TEST_F(SnapCtrlResumeCommitTest, AgentMaterializeFailureNeverCreatesClientOrCommitsRunning)
{
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
        testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .WillOnce(Invoke([](const std::shared_ptr<messages::ScheduleRequest> &,
                            const resume_identity::TrustedResumeIdentity &) {
            messages::DeployInstanceResponse response;
            response.set_code(static_cast<int32_t>(StatusCode::FILE_NOT_FOUND));
            response.set_message("FunctionAgent did not find the immutable snapshot");
            return litebus::Future<messages::DeployInstanceResponse>(response);
        }));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ReleasePausedInstanceResources(_))
        .WillOnce(Invoke([](const resource_view::InstanceInfo &instance) {
            EXPECT_EQ(instance.instanceid(), INSTANCE_ID);
            EXPECT_EQ(instance.functionagentid(), "target-agent-a");
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = StartResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
    EXPECT_EQ(storage_->StatCalls(), 0);
    EXPECT_EQ(storage_->GetCalls(), 0);
    EXPECT_EQ(storage_->DeleteCalls(), 0);
}

TEST_F(SnapCtrlResumeCommitTest, LoserResourceReleaseRetriesBeforeCompletingFailedResume)
{
    auto firstRelease = std::make_shared<litebus::Promise<Status>>();
    std::atomic<int> releaseCalls{ 0 };
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
        testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .WillOnce(Invoke([](const std::shared_ptr<messages::ScheduleRequest> &,
                            const resume_identity::TrustedResumeIdentity &) {
            messages::DeployInstanceResponse response;
            response.set_code(static_cast<int32_t>(StatusCode::FILE_NOT_FOUND));
            response.set_message("immutable snapshot is unavailable");
            return litebus::Future<messages::DeployInstanceResponse>(response);
        }));
    EXPECT_CALL(*instanceCtrl_, ReleasePausedInstanceResources(_))
        .Times(2)
        .WillOnce(Invoke([&](const resource_view::InstanceInfo &) {
            releaseCalls.fetch_add(1);
            return firstRelease->GetFuture();
        }))
        .WillOnce(Invoke([&](const resource_view::InstanceInfo &instance) {
            releaseCalls.fetch_add(1);
            EXPECT_EQ(instance.instanceid(), INSTANCE_ID);
            return litebus::Future<Status>(Status::OK());
        }));

    auto response = StartResume();

    ASSERT_AWAIT_TRUE_FOR([&releaseCalls]() { return releaseCalls.load() == 1; }, 5000);
    EXPECT_TRUE(response.IsInit());
    firstRelease->SetValue(Status(StatusCode::FAILED, "resource view release failed"));
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(releaseCalls.load(), 2);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
}

TEST_F(SnapCtrlResumeCommitTest, ProxyHasNoCheckpointRootOrStorageDependency)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &,
                                const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
}

TEST_F(SnapCtrlResumeCommitTest, ResumeRunningCommitRetainsPreviousSnapshotInfoUntilDeleteConfirmed)
{
    agentMgr_->HoldFinalizeResponse();
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            EXPECT_NE(context.scheduleReq, nullptr);
            if (context.scheduleReq == nullptr) {
                return litebus::Future<TransitionResult>(TransitionResult{
                    InstanceState::PAUSED, {}, {}, INSTANCE_VERSION, Status(StatusCode::FAILED) });
            }
            EXPECT_TRUE(context.scheduleReq->instance().has_snapshotinfo());
            EXPECT_TRUE(resume_identity::SnapshotIdentityMatches(
                context.scheduleReq->instance().snapshotinfo(), identity_->snapshot));
            stateMachine_->UpdateInstanceInfo(RetainedSnapshotWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            EXPECT_EQ(context.newState, InstanceState::RUNNING);
            EXPECT_EQ(context.version, INSTANCE_VERSION + 1);
            EXPECT_NE(context.scheduleReq, nullptr);
            if (context.scheduleReq == nullptr) {
                return litebus::Future<TransitionResult>(TransitionResult{
                    InstanceState::RUNNING, {}, {}, INSTANCE_VERSION + 1, Status(StatusCode::FAILED) });
            }
            EXPECT_FALSE(context.scheduleReq->instance().has_snapshotinfo());
            auto cleared = context.scheduleReq->instance();
            cleared.set_version(INSTANCE_VERSION + 2);
            stateMachine_->UpdateInstanceInfo(cleared);
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::RUNNING, {}, {}, INSTANCE_VERSION + 2, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();
    ASSERT_AWAIT_TRUE_FOR([this]() { return agentMgr_->FinalizeRequests().size() == 1U; }, 5000);
    const auto running = stateMachine_->GetInstanceInfo();
    EXPECT_EQ(running.instancestatus().code(), static_cast<int32_t>(InstanceState::RUNNING));
    EXPECT_EQ(running.version(), INSTANCE_VERSION + 1);
    ASSERT_TRUE(running.has_snapshotinfo());
    EXPECT_EQ(running.snapshotinfo().checkpointid(), "snapshot-a");
    ASSERT_AWAIT_NO_SET_FOR(response, 50);

    agentMgr_->CompleteFinalizeResponse(static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_FALSE(stateMachine_->GetInstanceInfo().has_snapshotinfo());
}

TEST_F(SnapCtrlResumeCommitTest, RemoteDeleteFailureDoesNotRollbackRunningAndKeepsSnapshotInfo)
{
    agentMgr_->SetFinalizeResponse(static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE), true, false, true);
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(RetainedSnapshotWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }));
    std::atomic<int> releaseCalls{0};
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([&releaseCalls](const resources::InstanceInfo &, const std::string &) {
            releaseCalls.fetch_add(1);
            return litebus::Future<Status>(Status::OK());
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    const auto running = stateMachine_->GetInstanceInfo();
    EXPECT_EQ(running.instancestatus().code(), static_cast<int32_t>(InstanceState::RUNNING));
    EXPECT_EQ(running.version(), INSTANCE_VERSION + 1);
    ASSERT_TRUE(running.has_snapshotinfo());
    EXPECT_EQ(running.snapshotinfo().checkpointid(), "snapshot-a");
    EXPECT_EQ(releaseCalls.load(), 0);
}

TEST_F(SnapCtrlResumeCommitTest, DeleteNotFoundClearsOnlySameGenerationAndSnapshot)
{
    agentMgr_->SetFinalizeResponse(static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(RetainedSnapshotWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            EXPECT_EQ(context.version, INSTANCE_VERSION + 1);
            EXPECT_NE(context.scheduleReq, nullptr);
            if (context.scheduleReq == nullptr) {
                return litebus::Future<TransitionResult>(TransitionResult{
                    InstanceState::RUNNING, {}, {}, INSTANCE_VERSION + 1, Status(StatusCode::FAILED) });
            }
            EXPECT_FALSE(context.scheduleReq->instance().has_snapshotinfo());
            auto cleared = context.scheduleReq->instance();
            cleared.set_version(INSTANCE_VERSION + 2);
            stateMachine_->UpdateInstanceInfo(cleared);
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::RUNNING, {}, {}, INSTANCE_VERSION + 2, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().expectedstorage(), "obs");
    EXPECT_FALSE(stateMachine_->GetInstanceInfo().has_snapshotinfo());
}

TEST_F(SnapCtrlResumeCommitTest, LateDeleteCallbackCannotClearNewPauseSnapshot)
{
    agentMgr_->HoldFinalizeResponse();
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(RetainedSnapshotWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();
    ASSERT_AWAIT_TRUE_FOR([this]() { return agentMgr_->FinalizeRequests().size() == 1U; }, 5000);
    auto newPause = stateMachine_->GetInstanceInfo();
    newPause.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
    newPause.set_version(INSTANCE_VERSION + 2);
    newPause.mutable_snapshotinfo()->set_checkpointid("snapshot-next-pause");
    newPause.mutable_snapshotinfo()->set_sha256("next-pause-sha256");
    stateMachine_->UpdateInstanceInfo(newPause);

    agentMgr_->CompleteFinalizeResponse(static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_TRUE(stateMachine_->GetInstanceInfo().has_snapshotinfo());
    EXPECT_EQ(stateMachine_->GetInstanceInfo().snapshotinfo().checkpointid(), "snapshot-next-pause");
}

TEST_F(SnapCtrlResumeCommitTest, OldResumeCleanupCannotDeleteNewPauseSnapshot)
{
    agentMgr_->HoldFinalizeResponse();
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &,
                                const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(RetainedSnapshotWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();
    ASSERT_AWAIT_TRUE_FOR([this]() { return agentMgr_->FinalizeRequests().size() == 1U; }, 5000);
    const auto &cleanup = agentMgr_->FinalizeRequests().front();
    EXPECT_EQ(cleanup.snapshotid(), identity_->snapshot.checkpointid());
    EXPECT_EQ(cleanup.expectedsha256(), identity_->snapshot.sha256());

    auto nextPause = stateMachine_->GetInstanceInfo();
    nextPause.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::PAUSED));
    nextPause.set_version(INSTANCE_VERSION + 2);
    nextPause.mutable_snapshotinfo()->set_checkpointid("snapshot-created-after-resume");
    nextPause.mutable_snapshotinfo()->set_sha256("snapshot-created-after-resume-sha");
    stateMachine_->UpdateInstanceInfo(nextPause);

    agentMgr_->CompleteFinalizeResponse(static_cast<int32_t>(StatusCode::SUCCESS), true, true);
    ASSERT_AWAIT_READY(response);
    ASSERT_TRUE(stateMachine_->GetInstanceInfo().has_snapshotinfo());
    EXPECT_EQ(stateMachine_->GetInstanceInfo().snapshotinfo().checkpointid(),
              "snapshot-created-after-resume");
}

TEST_F(SnapCtrlResumeCommitTest, FailedImmediateDeleteKeepsExactSnapshotInfoOnRunning)
{
    agentMgr_->SetFinalizeResponse(
        static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE), true, false, true);
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &,
                                const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(RetainedSnapshotWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{
                InstanceState::PAUSED, {}, {}, INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(
        INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().snapshotid(), identity_->snapshot.checkpointid());
    const auto running = stateMachine_->GetInstanceInfo();
    EXPECT_EQ(running.instancestatus().code(), static_cast<int32_t>(InstanceState::RUNNING));
    ASSERT_TRUE(running.has_snapshotinfo());
    EXPECT_TRUE(resume_identity::SnapshotIdentityMatches(
        running.snapshotinfo(), identity_->snapshot));
}

TEST_F(SnapCtrlResumeCommitTest, DeleteWinsAfterReadyResumeCasLosesAndCleansCandidate)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &) {
            stateMachine_->authoritativeInfo.Clear();
            stateMachine_->hasAuthoritativeInfo = true;
            TransitionResult result;
            result.status = Status(StatusCode::SCHEDULE_CONFLICTED, "delete won CAS");
            return litebus::Future<TransitionResult>(result);
        }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(stateMachine_->syncCalls.load(), 1);
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
}

TEST_F(SnapCtrlResumeCommitTest, LoserCleanupRetriesAgentFinalizeAndNeverUsesProxyRuntimeRelease)
{
    agentMgr_->QueueFinalizeResponse(
        static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE), false, false, true);
    agentMgr_->QueueFinalizeResponse(
        static_cast<int32_t>(StatusCode::SUCCESS), true, false, false);
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &) {
            stateMachine_->authoritativeInfo.Clear();
            stateMachine_->hasAuthoritativeInfo = true;
            TransitionResult result;
            result.status = Status(StatusCode::SCHEDULE_CONFLICTED, "another resume attempt won");
            return litebus::Future<TransitionResult>(result);
        }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = BeginResume();

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 2U);
    EXPECT_FALSE(response.IsInit());
    if (!response.IsInit()) {
        EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    }
    for (const auto &request : agentMgr_->FinalizeRequests()) {
        EXPECT_EQ(request.operation(), ::messages::RESUME_ABORTED);
        EXPECT_EQ(request.runtimeid(), RuntimeID());
        EXPECT_EQ(request.attemptid(), identity_->targetAttemptID);
    }
}

// Kills the target-side ordinary-failure branch that deletes an attempt when
// Sandboxd reports Restore/re-List result-unknown. The same context must replay
// the same deterministic candidate and later converge.
TEST_F(SnapCtrlResumeCommitTest, ResultUnknownRestoreRetriesSameAttemptWithoutLoserCleanupAndConverges)
{
    std::atomic<int> deployCalls{0};
    const messages::ScheduleRequest *firstRequest = nullptr;
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
        testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .Times(2)
        .WillOnce(Invoke([this, &deployCalls, &firstRequest](
                             const std::shared_ptr<messages::ScheduleRequest> &received,
                             const resume_identity::TrustedResumeIdentity &identity) {
            deployCalls.fetch_add(1);
            firstRequest = received.get();
            EXPECT_EQ(identity.targetAttemptID, "target-attempt-a");
            messages::DeployInstanceResponse unknown;
            unknown.set_code(static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE));
            unknown.set_message("restore and re-list results unavailable");
            return litebus::Future<messages::DeployInstanceResponse>(unknown);
        }))
        .WillOnce(Invoke([this, &deployCalls, &firstRequest](
                             const std::shared_ptr<messages::ScheduleRequest> &received,
                             const resume_identity::TrustedResumeIdentity &identity) {
            deployCalls.fetch_add(1);
            EXPECT_EQ(received.get(), firstRequest);
            EXPECT_EQ(identity.targetAttemptID, "target-attempt-a");
            messages::DeployInstanceResponse response;
            response.set_code(common::ERR_NONE);
            response.set_runtimeid(RuntimeID());
            response.set_address("target-address-a");
            response.set_containerid("target-container-a");
            response.set_containerip("target-container-ip-a");
            response.set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
            response.set_timeinfo("target-start-a");
            return litebus::Future<messages::DeployInstanceResponse>(response);
        }));
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            operations_.emplace_back("cas");
            operations_.emplace_back("route");
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = StartResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(deployCalls.load(), 2);
    EXPECT_EQ(storage_->GetCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
}

TEST_F(SnapCtrlResumeCommitTest, DeployFutureErrorRetriesSameAttemptAndConvergesPhysicalState)
{
    std::atomic<int> deployCalls{0};
    const messages::ScheduleRequest *firstRequest = nullptr;
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
        testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .Times(2)
        .WillOnce(Invoke([this, &deployCalls, &firstRequest](
                             const std::shared_ptr<messages::ScheduleRequest> &received,
                             const resume_identity::TrustedResumeIdentity &identity) {
            deployCalls.fetch_add(1);
            firstRequest = received.get();
            EXPECT_EQ(identity.targetAttemptID, "target-attempt-a");
            litebus::Future<messages::DeployInstanceResponse> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE));
            return failed;
        }))
        .WillOnce(Invoke([this, &deployCalls, &firstRequest](
                             const std::shared_ptr<messages::ScheduleRequest> &received,
                             const resume_identity::TrustedResumeIdentity &identity) {
            deployCalls.fetch_add(1);
            EXPECT_EQ(received.get(), firstRequest);
            EXPECT_EQ(identity.targetAttemptID, "target-attempt-a");
            messages::DeployInstanceResponse response;
            response.set_code(common::ERR_NONE);
            response.set_runtimeid(RuntimeID());
            response.set_address("target-address-a");
            response.set_containerid("target-container-a");
            response.set_containerip("target-container-ip-a");
            response.set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
            response.set_timeinfo("target-start-a");
            return litebus::Future<messages::DeployInstanceResponse>(response);
        }));
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = StartResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(deployCalls.load(), 2);
    EXPECT_EQ(storage_->GetCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
}

TEST_F(SnapCtrlResumeCommitTest, ResultUnknownAfterGenerationChangePreservesAttemptForSameCandidateReconcile)
{
    litebus::Promise<messages::DeployInstanceResponse> unexpectedSecondRestore;
    std::atomic<int> downstreamRestoreDispatches{0};
    const messages::ScheduleRequest *firstRequest = nullptr;
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
        testing::_, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this, &unexpectedSecondRestore, &downstreamRestoreDispatches, &firstRequest](
                                   const std::shared_ptr<messages::ScheduleRequest> &received,
                                   const resume_identity::TrustedResumeIdentity &identity) {
            const auto call = downstreamRestoreDispatches.fetch_add(1) + 1;
            EXPECT_EQ(identity.targetAttemptID, "target-attempt-a");
            if (call == 1) {
                firstRequest = received.get();
                auto replacement = stateMachine_->GetInstanceInfo();
                replacement.set_requestid("replacement-generation");
                replacement.set_version(INSTANCE_VERSION + 1);
                stateMachine_->UpdateInstanceInfo(replacement);
                messages::DeployInstanceResponse unknown;
                unknown.set_code(static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE));
                unknown.set_message("Restore result unknown during generation change");
                return litebus::Future<messages::DeployInstanceResponse>(unknown);
            }
            EXPECT_EQ(received.get(), firstRequest);
            return unexpectedSecondRestore.GetFuture();
        }));
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = StartResume();

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(downstreamRestoreDispatches.load(), 1);
    EXPECT_EQ(storage_->GetCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_FALSE(response.IsInit());
    if (!response.IsInit()) {
        EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    }
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().runtimeid(), RuntimeID());
}

TEST_F(SnapCtrlResumeCommitTest, AuthoritativeTerminalStateLoserCleansOnlyExactCandidateAndAttempt)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &) {
            auto terminal = stateMachine_->GetInstanceInfo();
            terminal.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::EXITED));
            terminal.set_version(INSTANCE_VERSION + 1);
            terminal.clear_snapshotinfo();
            stateMachine_->authoritativeInfo = terminal;
            stateMachine_->hasAuthoritativeInfo = true;
            TransitionResult result;
            result.status = Status(StatusCode::SCHEDULE_CONFLICTED, "terminal winner");
            return litebus::Future<TransitionResult>(result);
    }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
}

TEST_F(SnapCtrlResumeCommitTest, MissingInstanceLoserCleansOnlyExactCandidateAndAttempt)
{
    std::atomic<bool> missing { false };
    EXPECT_CALL(*instanceControlView_, GetInstance(INSTANCE_ID))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this, &missing](const std::string &) {
            return missing.load() ? std::shared_ptr<InstanceStateMachine>{}
                                  : std::static_pointer_cast<InstanceStateMachine>(stateMachine_);
        }));
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([&missing](const std::shared_ptr<InstanceStateMachine> &, const TransContext &) {
            missing = true;
            TransitionResult result;
            result.status = Status(StatusCode::SCHEDULE_CONFLICTED, "instance deleted");
            return litebus::Future<TransitionResult>(result);
    }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = BeginResume();

    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
}

TEST_F(SnapCtrlResumeCommitTest, ResumeDoesNotHeartbeatPublishRouteOrRunBeforeSnapStarted)
{
    litebus::Promise<std::shared_ptr<ControlInterfacePosixClient>> pendingClient;
    std::atomic<bool> clientRequested = false;
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(INSTANCE_ID, RuntimeID(), "target-address-a"))
        .WillOnce(Invoke([this, &pendingClient, &clientRequested](
                             const std::string &, const std::string &, const std::string &) {
            operations_.emplace_back("client");
            clientRequested = true;
            return pendingClient.GetFuture();
        }));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);

    auto response = BeginResume();
    ASSERT_AWAIT_TRUE_FOR([&clientRequested]() { return clientRequested.load(); }, 5000);
    ASSERT_AWAIT_NO_SET_FOR(response, 50);
    EXPECT_THAT(operations_, ElementsAre("restore", "deploy", "client"));

    auto replacement = stateMachine_->GetInstanceInfo();
    replacement.set_requestid("replacement-before-client-callback");
    stateMachine_->UpdateInstanceInfo(replacement);
    pendingClient.SetValue(std::shared_ptr<ControlInterfacePosixClient>(client_));
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(client_->SnapStartedCalls(), 0);
}

TEST_F(SnapCtrlResumeCommitTest, SnapStartedFailureFinalizesExactCandidateAndAttemptThroughAgent)
{
    client_->MakeSnapStartedPending();
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);

    auto response = BeginResume();
    ASSERT_AWAIT_TRUE_FOR([this]() { return client_->SnapStartedCalls() == 1; }, 5000);
    auto replacement = stateMachine_->GetInstanceInfo();
    replacement.set_requestid("replacement-before-snapstarted-callback");
    stateMachine_->UpdateInstanceInfo(replacement);
    client_->CompleteSnapStarted(common::ERR_NONE);
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
}

TEST_F(SnapCtrlResumeCommitTest, ReadyCommitsPausedNToRunningNPlusOneWithTargetPhysicalIdentity)
{
    EXPECT_CALL(*instanceCtrl_, ReleasePausedInstanceResources(_)).Times(0);
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &) {
            operations_.emplace_back("cas");
            litebus::Future<TransitionResult> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::ERR_ETCD_OPERATION_ERROR));
            return failed;
        }))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            operations_.emplace_back("cas");
            EXPECT_EQ(context.newState, InstanceState::RUNNING);
            EXPECT_EQ(context.version, INSTANCE_VERSION);
            EXPECT_NE(context.scheduleReq, nullptr);
            const auto &candidate = context.scheduleReq->instance();
            EXPECT_EQ(candidate.instanceid(), INSTANCE_ID);
            EXPECT_EQ(candidate.requestid(), INSTANCE_REQUEST_ID);
            EXPECT_EQ(candidate.functionproxyid(), OWNER_PROXY_ID);
            EXPECT_EQ(candidate.runtimeid(), RuntimeID());
            EXPECT_EQ(candidate.runtimeaddress(), "target-address-a");
            EXPECT_EQ(candidate.functionagentid(), "target-agent-a");
            EXPECT_EQ(candidate.containerid(), "target-container-a");
            EXPECT_EQ(candidate.containerip(), "target-container-ip-a");
            EXPECT_EQ(candidate.unitid(), "target-unit-a");
            EXPECT_EQ(candidate.executortype(), static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
            EXPECT_TRUE(candidate.has_snapshotinfo());
            if (candidate.has_snapshotinfo()) {
                EXPECT_TRUE(resume_identity::SnapshotIdentityMatches(
                    candidate.snapshotinfo(), identity_->snapshot));
            }
            operations_.emplace_back("route");
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
}

TEST_F(SnapCtrlResumeCommitTest, EmptyTargetMappingsClearStaleSourcePortForwardBeforeRunningCommit)
{
    SeedStaleSourcePortForward();
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            if (context.scheduleReq == nullptr) {
                ADD_FAILURE() << "resume commit is missing its schedule request";
                return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                           INSTANCE_VERSION,
                                                                           Status(StatusCode::FAILED) });
            }
            EXPECT_EQ(context.scheduleReq->instance().extensions().count("portForward"), 0U);
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResumeWithMappings("");
    ASSERT_AWAIT_READY(response);
    ASSERT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));

    const auto committed = stateMachine_->GetInstanceInfo();
    global_scheduler::TraefikRouteCache routeCache(global_scheduler::TraefikConfig{});
    routeCache.OnInstanceRunning(committed);
    EXPECT_EQ(routeCache.GetRouteCount(), 0U);

    TcpTunnelServerConfig tunnelConfig;
    tunnelConfig.nodeID = OWNER_PROXY_ID;
    TcpTunnelServer tunnel(tunnelConfig, instanceControlView_, nullptr);
    std::string error;
    EXPECT_EQ(tunnel.ResolveHostPort(INSTANCE_ID, 22, error), -1);
    EXPECT_EQ(error, "instance has no port forward metadata");
}

TEST_F(SnapCtrlResumeCommitTest, NonEmptyTargetMappingsReplaceStaleSourcePortForwardAndConsumersUseTarget)
{
    const std::string targetMappings = R"(["http:41001:8080","tcp:41022:22"])";
    SeedStaleSourcePortForward();
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this, targetMappings](const std::shared_ptr<InstanceStateMachine> &,
                                                const TransContext &context) {
            if (context.scheduleReq == nullptr) {
                ADD_FAILURE() << "resume commit is missing its schedule request";
                return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                           INSTANCE_VERSION,
                                                                           Status(StatusCode::FAILED) });
            }
            const auto mapping = context.scheduleReq->instance().extensions().find("portForward");
            if (mapping == context.scheduleReq->instance().extensions().end()) {
                ADD_FAILURE() << "resume candidate has no target port mapping";
            } else {
                EXPECT_EQ(mapping->second, targetMappings);
            }
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResumeWithMappings(targetMappings);
    ASSERT_AWAIT_READY(response);
    ASSERT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));

    const auto committed = stateMachine_->GetInstanceInfo();
    global_scheduler::TraefikRouteCache routeCache(global_scheduler::TraefikConfig{});
    routeCache.OnInstanceRunning(committed);
    EXPECT_EQ(routeCache.GetRouteCount(), 2U);
    EXPECT_THAT(routeCache.GetConfigJSON(), HasSubstr("10.0.0.9:41001"));
    EXPECT_THAT(routeCache.GetConfigJSON(), Not(HasSubstr("39001")));

    TcpTunnelServerConfig tunnelConfig;
    tunnelConfig.nodeID = OWNER_PROXY_ID;
    TcpTunnelServer tunnel(tunnelConfig, instanceControlView_, nullptr);
    std::string error;
    EXPECT_EQ(tunnel.ResolveHostPort(INSTANCE_ID, 22, error), 41022);
    EXPECT_EQ(tunnel.ResolveHostPort(INSTANCE_ID, 2222, error), -1);
}

TEST_F(SnapCtrlResumeCommitTest, CasConflictSyncsAuthoritativeWinnerBeforeClassifying)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            operations_.emplace_back("cas");
            stateMachine_->authoritativeInfo = ExactWinner(context);
            stateMachine_->hasAuthoritativeInfo = true;
            auto replacement = stateMachine_->GetInstanceInfo();
            replacement.set_requestid("replacement-generation");
            stateMachine_->UpdateInstanceInfo(replacement);
            TransitionResult result;
            result.status = Status(StatusCode::SCHEDULE_CONFLICTED, "cas conflict");
            return litebus::Future<TransitionResult>(result);
        }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = BeginResume();
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(stateMachine_->syncCalls.load(), 1);
}

TEST_F(SnapCtrlResumeCommitTest, ExactSameWinnerAfterUnknownCasReturnsSuccessIdempotently)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            operations_.emplace_back("cas");
            stateMachine_->authoritativeInfo = ExactWinner(context);
            (*stateMachine_->authoritativeInfo.mutable_extensions())["updateTimestamp"] = "authoritative-only";
            stateMachine_->hasAuthoritativeInfo = true;
            operations_.emplace_back("route");
            litebus::Future<TransitionResult> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::ERR_ETCD_OPERATION_ERROR));
            return failed;
        }));
    stateMachine_->syncFutureFailuresRemaining = 1;
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(stateMachine_->syncCalls.load(), 2);
    EXPECT_LT(std::find(operations_.begin(), operations_.end(), "route"),
              std::find(operations_.begin(), operations_.end(), "heartbeat"));
}

TEST_F(SnapCtrlResumeCommitTest, DifferentWinnerOrDeleteWinnerReleasesCandidateAndNeverDeletesSourceRemote)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &) {
            operations_.emplace_back("cas");
            auto other = stateMachine_->GetInstanceInfo();
            other.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
            other.set_version(INSTANCE_VERSION + 1);
            other.set_starttime("different-start");
            other.set_functionagentid("different-agent");
            other.clear_snapshotinfo();
            stateMachine_->authoritativeInfo = other;
            stateMachine_->hasAuthoritativeInfo = true;
            litebus::Future<TransitionResult> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::ERR_ETCD_OPERATION_ERROR));
            return failed;
        }));
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(_, _, _, _)).Times(0);

    auto response = BeginResume();
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_ABORTED);
}

TEST_F(SnapCtrlResumeCommitTest, RouteAndHeartbeatStartOnlyAfterSuccessfulRunningCas)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            operations_.emplace_back("cas");
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);

    auto response = BeginResume();
    ASSERT_AWAIT_READY(response);
    EXPECT_THAT(operations_, ElementsAre("restore", "deploy", "snapstarted", "cas", "heartbeat"));
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
}

TEST_F(SnapCtrlResumeCommitTest, CleanupFailureAfterWinnerDoesNotRollbackOrRetry)
{
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .Times(1)
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            operations_.emplace_back("cas");
            operations_.emplace_back("route");
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);

    auto response = BeginResume();
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
    ASSERT_TRUE(stateMachine_->GetInstanceInfo().has_snapshotinfo());
    EXPECT_EQ(stateMachine_->GetInstanceInfo().snapshotinfo().checkpointid(), "snapshot-a");
}

TEST_F(SnapCtrlResumeCommitTest, SameAttemptReplayOnSameSnapCtrlCoalescesOneCandidateAndBothCallers)
{
    EXPECT_CALL(*instanceCtrl_, DeploySnapStartInstance(
        _, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .Times(1)
        .WillOnce(Invoke([this](const std::shared_ptr<messages::ScheduleRequest> &,
                                const resume_identity::TrustedResumeIdentity &) {
            messages::DeployInstanceResponse response;
            response.set_code(common::ERR_NONE);
            response.set_runtimeid(RuntimeID());
            response.set_address("target-address-a");
            response.set_containerid("target-container-a");
            response.set_containerip("target-container-ip-a");
            response.set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
            response.set_timeinfo("target-start-a");
            return litebus::Future<messages::DeployInstanceResponse>(response);
        }));
    EXPECT_CALL(*instanceCtrl_, CreateInstanceClient(_, _, _)).Times(1).WillOnce(Return(client_));
    EXPECT_CALL(*instanceCtrl_, TransInstanceState(_, _))
        .Times(1)
        .WillOnce(Invoke([this](const std::shared_ptr<InstanceStateMachine> &, const TransContext &context) {
            stateMachine_->UpdateInstanceInfo(ExactWinner(context));
            return litebus::Future<TransitionResult>(TransitionResult{ InstanceState::PAUSED, {}, {},
                                                                       INSTANCE_VERSION + 1, Status::OK() });
        }));
    EXPECT_CALL(*instanceCtrl_, StartHeartbeat(INSTANCE_ID, 0, RuntimeID(), StatusCode::SUCCESS)).Times(1);
    EXPECT_CALL(*instanceCtrl_, ReleaseRuntimeForPause(_, _)).Times(0);

    auto firstPromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    auto replayPromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    TransitionResult paused;
    paused.preState = InstanceState::PAUSED;
    paused.previousInfo = stateMachine_->GetInstanceInfo();
    paused.version = INSTANCE_VERSION;
    actor_->SnapStart(firstPromise, request_, schedule_decision::ScheduleResult{}, paused, identity_);
    actor_->SnapStart(replayPromise, request_, schedule_decision::ScheduleResult{}, paused, identity_);

    ASSERT_AWAIT_READY(firstPromise->GetFuture());
    ASSERT_AWAIT_READY(replayPromise->GetFuture());
    EXPECT_EQ(firstPromise->GetFuture().Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(replayPromise->GetFuture().Get().SerializeAsString(),
              firstPromise->GetFuture().Get().SerializeAsString());
    EXPECT_EQ(storage_->GetCalls(), 0);
    EXPECT_EQ(storage_->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr_->FinalizeRequests().size(), 1U);
    EXPECT_EQ(agentMgr_->FinalizeRequests().front().operation(), ::messages::RESUME_COMMITTED);
    EXPECT_TRUE(stateMachine_->GetInstanceInfo().has_snapshotinfo());
}

TEST(SnapCtrlResumeRaceTest, TwoResumeAttemptsCleanupOnlyDeterministicLoser)
{
    auto paused = MakeInstanceInfo(InstanceState::PAUSED, INSTANCE_MANAGER_OWNER);
    paused.set_tenantid("tenant-a");
    auto *snapshot = paused.mutable_snapshotinfo();
    snapshot->set_checkpointid("snapshot-a");
    snapshot->set_status(resources::SNAPSHOT_READY);
    snapshot->set_storage("obs");
    snapshot->set_size(sizeof(ResumeBoundarySnapshotStorage::PAYLOAD) - 1);
    snapshot->set_sha256(resume_identity::Sha256Hex(ResumeBoundarySnapshotStorage::PAYLOAD));
    snapshot->set_createtime("1786672800");
    snapshot->set_ttlseconds(90000);
    auto stateMachine = MakeInstanceStateMachine(paused);
    auto view = std::make_shared<MockInstanceControlView>(OWNER_PROXY_ID);
    EXPECT_CALL(*view, GetInstance(INSTANCE_ID)).Times(AnyNumber()).WillRepeatedly(Return(stateMachine));

    auto storage = std::make_shared<ResumeBoundarySnapshotStorage>(nullptr, INSTANCE_VERSION - 1);
    auto ctrl = std::make_shared<MockInstanceCtrl>();
    auto clientA = std::make_shared<ControllablePrepareSnapClient>();
    auto clientB = std::make_shared<ControllablePrepareSnapClient>();
    clientA->ConfigureSnapStarted(common::ERR_NONE, nullptr);
    clientB->ConfigureSnapStarted(common::ERR_NONE, nullptr);
    auto actor = std::make_shared<SnapCtrlActor>("resume-race-same-actor", OWNER_PROXY_ID);
    actor->BindInstanceControlView(view);
    actor->BindInstanceCtrl(ctrl);
    auto agentMgr = std::make_shared<SnapshotRuntimeProbe>();
    agentMgr->SetFinalizeResponse(
        static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE), true, false, true);
    actor->BindFunctionAgentMgr(agentMgr);
    litebus::Spawn(actor);
    struct ScopedActor {
        std::shared_ptr<SnapCtrlActor> actor;
        ~ScopedActor()
        {
            if (actor != nullptr) {
                litebus::Terminate(actor->GetAID());
                litebus::Await(actor->GetAID());
            }
        }
    } cleanup{ actor };

    auto makeAttempt = [&paused](const std::string &attempt, const std::string &agent,
                                 const std::string &unit) {
        auto request = std::make_shared<messages::ScheduleRequest>();
        request->set_requestid(attempt);
        request->mutable_instance()->CopyFrom(paused);
        request->mutable_instance()->set_functionproxyid(OWNER_PROXY_ID);
        request->mutable_instance()->set_functionagentid(agent);
        request->mutable_instance()->set_unitid(unit);
        auto identity = std::make_shared<resume_identity::TrustedResumeIdentity>(
            resume_identity::TrustedResumeIdentity::FromSchedule(*request));
        return std::make_pair(request, identity);
    };
    auto [requestA, identityA] = makeAttempt("target-attempt-a", "target-agent-a", "target-unit-a");
    auto [requestB, identityB] = makeAttempt("target-attempt-b", "target-agent-b", "target-unit-b");

    auto deployResponse = [](const std::string &attempt, const std::string &address,
                             const std::string &container,
                             const resume_identity::TrustedResumeIdentity &identity) {
        messages::DeployInstanceResponse response;
        response.set_code(common::ERR_NONE);
        response.set_runtimeid(resume_identity::RuntimeID(identity.logicalInstanceID, attempt));
        response.set_address(address);
        response.set_containerid(container);
        response.set_containerip("target-ip-" + attempt);
        response.set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
        response.set_timeinfo("start-" + attempt);
        return response;
    };
    litebus::Promise<messages::DeployInstanceResponse> deployA;
    litebus::Promise<messages::DeployInstanceResponse> deployB;
    std::atomic<int> deployCalls { 0 };
    EXPECT_CALL(*ctrl, DeploySnapStartInstance(
        _, testing::An<const resume_identity::TrustedResumeIdentity &>()))
        .Times(2)
        .WillRepeatedly(Invoke([&](const std::shared_ptr<messages::ScheduleRequest> &,
                                  const resume_identity::TrustedResumeIdentity &identity) {
            ++deployCalls;
            return identity.targetAttemptID == "target-attempt-a"
                ? deployA.GetFuture()
                : deployB.GetFuture();
        }));
    EXPECT_CALL(*ctrl, CreateInstanceClient(_, _, _))
        .Times(2)
        .WillRepeatedly(Invoke([&](const std::string &, const std::string &runtimeID, const std::string &) {
            return litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>(
                runtimeID == resume_identity::RuntimeID(INSTANCE_ID, "target-attempt-a")
                    ? std::static_pointer_cast<ControlInterfacePosixClient>(clientA)
                    : std::static_pointer_cast<ControlInterfacePosixClient>(clientB));
        }));

    litebus::Promise<TransitionResult> casA;
    litebus::Promise<TransitionResult> casB;
    std::atomic<int> casCalls { 0 };
    std::shared_ptr<messages::ScheduleRequest> runningA;
    EXPECT_CALL(*ctrl, TransInstanceState(_, _))
        .Times(2)
        .WillRepeatedly(Invoke([&](const std::shared_ptr<InstanceStateMachine> &,
                                  const TransContext &context) {
            ++casCalls;
            if (context.scheduleReq->requestid() == "target-attempt-a") {
                runningA = std::make_shared<messages::ScheduleRequest>(*context.scheduleReq);
                return casA.GetFuture();
            }
            return casB.GetFuture();
        }));
    std::atomic<int> winnerHeartbeats { 0 };
    EXPECT_CALL(*ctrl, StartHeartbeat(INSTANCE_ID, 0,
        resume_identity::RuntimeID(INSTANCE_ID, "target-attempt-a"), StatusCode::SUCCESS))
        .WillOnce(InvokeWithoutArgs([&]() { ++winnerHeartbeats; }));
    EXPECT_CALL(*ctrl, ReleaseRuntimeForPause(_, _)).Times(0);
    EXPECT_CALL(*ctrl, ReleasePausedInstanceResources(_))
        .WillOnce(Invoke([](const resource_view::InstanceInfo &instance) {
            EXPECT_EQ(instance.instanceid(), INSTANCE_ID);
            EXPECT_EQ(instance.functionagentid(), "target-agent-b");
            EXPECT_EQ(instance.runtimeid(),
                      resume_identity::RuntimeID(INSTANCE_ID, "target-attempt-b"));
            return litebus::Future<Status>(Status::OK());
        }));

    auto resultA = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    auto resultB = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    TransitionResult pausedTransition;
    pausedTransition.preState = InstanceState::PAUSED;
    pausedTransition.previousInfo = paused;
    pausedTransition.version = INSTANCE_VERSION;
    actor->SnapStart(resultA, requestA, schedule_decision::ScheduleResult{}, pausedTransition, identityA);
    actor->SnapStart(resultB, requestB, schedule_decision::ScheduleResult{}, pausedTransition, identityB);
    ASSERT_EQ(deployCalls.load(), 2);
    deployA.SetValue(deployResponse("target-attempt-a", "address-a", "container-a", *identityA));
    deployB.SetValue(deployResponse("target-attempt-b", "address-b", "container-b", *identityB));
    ASSERT_AWAIT_TRUE_FOR([&]() { return casCalls.load() == 2; }, 5000);
    ASSERT_NE(runningA, nullptr);

    auto winner = runningA->instance();
    winner.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    winner.set_version(INSTANCE_VERSION + 1);
    *winner.mutable_snapshotinfo() = paused.snapshotinfo();
    stateMachine->UpdateInstanceInfo(winner);
    stateMachine->authoritativeInfo = winner;
    stateMachine->hasAuthoritativeInfo = true;
    TransitionResult winnerResult;
    winnerResult.preState = InstanceState::PAUSED;
    winnerResult.version = INSTANCE_VERSION + 1;
    winnerResult.status = Status::OK();
    casA.SetValue(winnerResult);
    TransitionResult loserResult;
    loserResult.status = Status(StatusCode::SCHEDULE_CONFLICTED, "attempt-a won");
    casB.SetValue(loserResult);

    ASSERT_AWAIT_READY(resultA->GetFuture());
    ASSERT_AWAIT_READY(resultB->GetFuture());
    EXPECT_EQ(resultA->GetFuture().Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_NE(resultB->GetFuture().Get().code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(winnerHeartbeats.load(), 1);
    EXPECT_EQ(storage->DeleteCalls(), 0);
    ASSERT_EQ(agentMgr->FinalizeRequests().size(), 2U);
    const auto committed = std::find_if(
        agentMgr->FinalizeRequests().begin(), agentMgr->FinalizeRequests().end(),
        [](const ::messages::SnapshotAttemptFinalizeRequest &request) {
            return request.operation() == ::messages::RESUME_COMMITTED;
        });
    const auto aborted = std::find_if(
        agentMgr->FinalizeRequests().begin(), agentMgr->FinalizeRequests().end(),
        [](const ::messages::SnapshotAttemptFinalizeRequest &request) {
            return request.operation() == ::messages::RESUME_ABORTED;
        });
    ASSERT_NE(committed, agentMgr->FinalizeRequests().end());
    ASSERT_NE(aborted, agentMgr->FinalizeRequests().end());
    EXPECT_EQ(committed->attemptid(), "target-attempt-a");
    EXPECT_EQ(aborted->attemptid(), "target-attempt-b");
    EXPECT_EQ(aborted->runtimeid(), resume_identity::RuntimeID(INSTANCE_ID, "target-attempt-b"));
    EXPECT_EQ(stateMachine->GetInstanceInfo().runtimeid(),
              resume_identity::RuntimeID(INSTANCE_ID, "target-attempt-a"));
    EXPECT_TRUE(stateMachine->GetInstanceInfo().has_snapshotinfo());

}

}  // namespace
}  // namespace functionsystem::test
