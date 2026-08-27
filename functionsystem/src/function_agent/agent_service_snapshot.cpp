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

#include "agent_service_actor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "common/snapshot_storage/snapshot_artifact_publisher.h"
#include "common/utils/resume_identity.h"
#include "runtime_manager/ckpt/checkpoint_plan.h"

namespace functionsystem::function_agent {
namespace {
constexpr uint32_t SNAPSHOT_ATTEMPT_PROTOCOL_VERSION = 1;
constexpr char SANDBOX_RUNTIME_CLASS[] = "runsc";

Status EnsureLocalSnapshotID(messages::SnapshotRuntimeRequest &request)
{
    if (request.snapshotid().empty()) {
        const auto identity = request.requestid() + std::string(1, '\0')
            + request.instanceid();
        request.set_snapshotid("ckpt-" + resume_identity::Sha256Hex(identity).substr(0, 40));
    }
    if (!runtime_manager::IsSafeCheckpointIdentityComponent(request.snapshotid())) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "local snapshot identity is invalid");
    }
    return Status::OK();
}

LocalSnapshotCommitRequest MakeLocalSnapshotCommitRequest(
    const messages::SnapshotRuntimeRequest &request, int64_t createdAtUnixSeconds,
    const std::string &architecture)
{
    LocalSnapshotCommitRequest commit;
    commit.snapshotID = request.snapshotid();
    commit.anonymous = request.anonymous();
    commit.instanceID = request.instanceid();
    commit.tenantHash = snapshot_storage::StableTenantHash(request.tenantid());
    commit.sourceRuntimeID = request.runtimeid();
    commit.sourceSandboxID = request.containerid().empty() ? request.runtimeid() : request.containerid();
    commit.sourceInstanceVersion = request.sourceversion();
    commit.runtimeClass = SANDBOX_RUNTIME_CLASS;
    commit.architecture = architecture;
    commit.createdAtUnixSeconds = createdAtUnixSeconds;
    return commit;
}

void CopyLocalSnapshotMetadata(const LocalSnapshotDescriptor &source,
                               ::messages::LocalSnapshotMetadata &target)
{
    target.set_snapshotid(source.snapshotID);
    target.set_anonymous(source.anonymous);
    target.set_instanceid(source.instanceID);
    target.set_generation(source.generation);
    target.set_runtimeclass(source.runtimeClass);
    target.set_architecture(source.architecture);
    target.set_size(source.size);
    target.set_sha256(source.sha256);
    target.set_createdatunixseconds(source.createdAtUnixSeconds);
    target.set_sourceruntimeid(source.sourceRuntimeID);
    target.set_sourcesandboxid(source.sourceSandboxID);
    target.set_sourceinstanceversion(source.sourceInstanceVersion);
    target.set_tenanthash(source.tenantHash);
    target.set_artifactformat(source.artifactFormat);
    target.set_artifactformatversion(source.artifactFormatVersion);
}

struct ManagedSnapshotPublishSpec {
    std::string temporaryKey;
    std::string finalKey;
    int32_t ttlSeconds{ 0 };
};

ManagedSnapshotPublishSpec MakePauseResumePublishSpec(
    const messages::SnapshotRuntimeRequest &request, const std::string &tenantHash)
{
    return {
        snapshot_storage::BuildPauseSnapshotTemporaryKey(
            tenantHash, request.instanceid(), request.snapshotid(), request.requestid()),
        snapshot_storage::BuildPauseSnapshotKey(
            tenantHash, request.instanceid(), request.snapshotid()),
        request.ttl(),
    };
}

ManagedSnapshotPublishSpec MakeReusableSnapshotPublishSpec(
    const messages::SnapshotRuntimeRequest &request, const std::string &tenantHash,
    const std::string &frozenObjectKey)
{
    return {
        snapshot_storage::BuildReusableSnapshotTemporaryKey(
            tenantHash, request.snapshotid(), request.requestid()),
        frozenObjectKey,
        0,
    };
}

snapshot_storage::ArtifactPublishRequest BuildManagedSnapshotPublishRequest(
    const messages::SnapshotRuntimeRequest &request, const std::string &sourceFile,
    const std::string &frozenReusableObjectKey, int64_t createdAtUnixSeconds,
    const resources::SnapshotInfo &runtimeSnapshot)
{
    const auto tenantHash = snapshot_storage::StableTenantHash(
        request.tenantid());
    const auto spec = request.type() == common::PAUSE_RESUME
        ? MakePauseResumePublishSpec(request, tenantHash)
        : MakeReusableSnapshotPublishSpec(request, tenantHash, frozenReusableObjectKey);
    snapshot_storage::ArtifactPublishRequest publishRequest;
    publishRequest.sourceFile = sourceFile;
    publishRequest.temporaryKey = spec.temporaryKey;
    publishRequest.finalKey = spec.finalKey;
    publishRequest.snapshotID = request.snapshotid();
    publishRequest.sourceInstanceVersion = request.sourceversion();
    publishRequest.createdAtUnixSeconds = createdAtUnixSeconds;
    publishRequest.ttlSeconds = spec.ttlSeconds;
    publishRequest.expectedSize = runtimeSnapshot.size();
    publishRequest.expectedSha256 = runtimeSnapshot.sha256();
    return publishRequest;
}

litebus::Future<snapshot_storage::ArtifactPublishResult> PublishManagedSnapshotArtifact(
    const std::shared_ptr<snapshot_storage::SnapshotStorage> &storage,
    const std::shared_ptr<ActorWorker> &worker,
    const snapshot_storage::ArtifactPublishRequest &request)
{
    return std::make_shared<snapshot_storage::SnapshotArtifactPublisher>(storage, worker)->Publish(request);
}

}  // namespace

void AgentServiceActor::SnapshotRuntime(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto request = std::make_shared<messages::SnapshotRuntimeRequest>();
    if (!request->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse SnapshotRuntimeRequest");
        return;
    }

    const std::string &instanceID = request->instanceid();
    const std::string &runtimeID = request->runtimeid();
    const bool isPauseResume = request->type() == common::PAUSE_RESUME;
    const bool isReusableSnapshot = request->type() == common::SNAPSHOT;
    const bool isManagedSnapshot = isPauseResume || isReusableSnapshot;
    YRLOG_INFO("{}|received SnapshotRuntime request for instance({}), runtime({})",
               request->requestid(), instanceID, runtimeID);

    // Prepare response
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(request->requestid());

    if (isManagedSnapshot && from != localSchedFuncAgentMgrAID_) {
        YRLOG_WARN("{}|reject managed SnapshotRuntime from untrusted sender {}",
                   request->requestid(), std::string(from));
        return;
    }

    // Check if agent is registered
    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        response.set_code(static_cast<int32_t>(StatusCode::FUNC_AGENT_NOT_REGISTERED));
        response.set_message("function agent is not registered");
        YRLOG_ERROR("{}|registration is not complete, ignore snapshot request for instance({})",
                    request->requestid(), instanceID);
        Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
        return;
    }

    const auto runtimeManagerAID = litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address);
    if (isPauseResume) {
        request->set_anonymous(true);
        request->set_leaverunning(true);
    }
    if (isReusableSnapshot) {
        request->set_anonymous(false);
        request->set_ttl(0);
        request->set_leaverunning(true);
    }
    const auto identityStatus = EnsureLocalSnapshotID(*request);
    if (identityStatus.IsError()) {
        response.set_code(static_cast<int32_t>(identityStatus.StatusCode()));
        response.set_message(identityStatus.RawMessage());
        Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
        return;
    }
    if (isManagedSnapshot) {
        if (request->requestid().empty() || request->instanceid().empty() || request->runtimeid().empty()
            || request->snapshotid().empty() || request->tenantid().empty()
            || request->sourceversion() <= 0
            || (isPauseResume && (request->snapshotid() != request->requestid() || request->ttl() <= 0
                                  || !request->anonymous() || !request->leaverunning()))
            || (isReusableSnapshot && (request->ttl() != 0 || !request->leaverunning()))
            || snapshotStorage_ == nullptr || snapshotWorker_ == nullptr) {
            response.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
            response.set_message("managed snapshot data-plane identity or dependency is missing");
            Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
            return;
        }
        const auto existing = snapshotRequests_.find(request->requestid());
        if (existing != snapshotRequests_.end()) {
            auto existingIdentity = existing->second.request;
            auto retryIdentity = *request;
            existingIdentity.clear_agentrequestgeneration();
            retryIdentity.clear_agentrequestgeneration();
            if (existing->second.request.type() == request->type()
                && existingIdentity.SerializeAsString() == retryIdentity.SerializeAsString()) {
                existing->second.caller = from;
                if (existing->second.completed) {
                    YRLOG_INFO("{}|replay completed managed SnapshotRuntime response", request->requestid());
                    Send(from, "SnapshotRuntimeResponse",
                         existing->second.completedResponse.SerializeAsString());
                } else {
                    YRLOG_INFO("{}|reuse in-flight managed SnapshotRuntime request", request->requestid());
                }
                return;
            }
            response.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
            response.set_message("conflicting in-flight managed snapshot request ID");
            Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
            return;
        }
        auto runtimeRequestGeneration = nextSnapshotRequestGeneration_++;
        if (runtimeRequestGeneration == 0) {
            runtimeRequestGeneration = nextSnapshotRequestGeneration_++;
        }
        request->set_agentrequestgeneration(runtimeRequestGeneration);
    } else {
        const auto existing = snapshotRequests_.find(request->requestid());
        if (existing != snapshotRequests_.end()
            && (existing->second.request.type() == common::PAUSE_RESUME
                || existing->second.request.type() == common::SNAPSHOT)) {
            response.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
            response.set_message("request ID belongs to an in-flight managed snapshot");
            Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
            return;
        }
    }

    PendingSnapshotRequest pending;
    pending.caller = from;
    pending.runtimeManagerAID = runtimeManagerAID;
    pending.runtimeManagerID = registerRuntimeMgr_.id;
    pending.createdAtUnixSeconds = static_cast<int64_t>(std::time(nullptr));
    if (localSnapshotStore_ == nullptr || registeredResourceUnit_ == nullptr
        || registeredResourceUnit_->systeminfo().architecture().empty()) {
        response.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
        response.set_message("local snapshot store or node architecture is unavailable");
        Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
        return;
    }
    pending.localCommitRequest = MakeLocalSnapshotCommitRequest(
        *request, pending.createdAtUnixSeconds,
        registeredResourceUnit_->systeminfo().architecture());
    if (isManagedSnapshot) {
        const auto tenantHash = snapshot_storage::StableTenantHash(request->tenantid());
        if (isReusableSnapshot) {
            pending.artifactObjectKey = snapshot_storage::BuildReusableSnapshotKey(
                tenantHash, request->snapshotid());
        }
        auto backendStatus = snapshot_storage::ResolveStorageBackend(
            snapshotStorage_, snapshotStorageBackend_, pending.storageBackend);
        if (backendStatus.IsError()) {
            response.set_code(static_cast<int32_t>(backendStatus.StatusCode()));
            response.set_message(backendStatus.ToString());
            Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
            return;
        }
    }

    const auto prepared = localSnapshotStore_->Prepare(pending.localCommitRequest);
    if (prepared.status.IsError()) {
        response.set_code(static_cast<int32_t>(prepared.status.StatusCode()));
        response.set_message(prepared.status.RawMessage());
        Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
        return;
    }
    request->set_checkpointdir(prepared.directory.string());
    pending.request = *request;
    pending.artifactPath = (prepared.directory / "checkpoint.img").string();
    if (isManagedSnapshot) {
        if (!snapshotRequests_.emplace(request->requestid(), std::move(pending)).second) {
            response.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
            response.set_message("duplicate in-flight managed snapshot request ID");
            Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
            return;
        }
    } else {
        snapshotRequests_.insert_or_assign(request->requestid(), std::move(pending));
    }

    if (prepared.replayed) {
        LocalSnapshotDescriptor descriptor;
        const auto replay = localSnapshotStore_->ValidateForRestore(request->snapshotid(), descriptor);
        if (replay.IsError()) {
            response.set_code(static_cast<int32_t>(replay.StatusCode()));
            response.set_message(replay.RawMessage());
            Send(from, "SnapshotRuntimeResponse", response.SerializeAsString());
            snapshotRequests_.erase(request->requestid());
            return;
        }
        auto &stored = snapshotRequests_.at(request->requestid());
        CopyLocalSnapshotMetadata(descriptor, stored.localSnapshot);
        stored.createdAtUnixSeconds = descriptor.createdAtUnixSeconds;
        response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        response.mutable_snapshotinfo()->set_checkpointid(descriptor.snapshotID);
        response.mutable_snapshotinfo()->set_size(static_cast<int64_t>(descriptor.size));
        response.mutable_snapshotinfo()->set_sha256(descriptor.sha256);
        ContinueSnapshotAfterLocalCommit(request->requestid(), std::move(response));
        return;
    }

    YRLOG_INFO("{}|forward SnapshotRuntime request to RuntimeManager({}-{}) for instance({}), runtime({})",
               request->requestid(), registerRuntimeMgr_.name, registerRuntimeMgr_.address, instanceID, runtimeID);
    ForwardSnapshotRuntimeRequest(request->requestid());
}

void AgentServiceActor::ForwardSnapshotRuntimeRequest(const std::string &requestID)
{
    const auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        return;
    }
    Send(iter->second.runtimeManagerAID, "SnapshotRuntime", iter->second.request.SerializeAsString());
}

void AgentServiceActor::SnapshotRuntimeResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::SnapshotRuntimeResponse response;
    if (!response.ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse SnapshotRuntimeResponse");
        return;
    }

    const std::string &requestID = response.requestid();
    auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        YRLOG_WARN("{}|snapshot request not found in snapshotRequests_", requestID);
        return;
    }

    auto &pending = iter->second;
    const bool isPauseResume = pending.request.type() == common::PAUSE_RESUME;
    const bool isReusableSnapshot = pending.request.type() == common::SNAPSHOT;
    if (from != pending.runtimeManagerAID
        || ((isPauseResume || isReusableSnapshot)
            && response.agentrequestgeneration() != pending.request.agentrequestgeneration())) {
        YRLOG_WARN("{}|ignore mismatched SnapshotRuntime response", requestID);
        return;
    }

    YRLOG_INFO("{}|received SnapshotRuntimeResponse from RuntimeManager, code: {}", requestID, response.code());

    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        if (isPauseResume || isReusableSnapshot) {
            if (response.resultunknown()) {
                response.clear_physicalfact();
            }
            response.clear_agentrequestgeneration();
            if (isReusableSnapshot && response.has_snapshotinfo()
                && response.snapshotinfo().size() > 0
                && !response.snapshotinfo().sha256().empty()
                && !pending.storageBackend.empty()
                && !pending.artifactObjectKey.empty()) {
                auto *artifact = response.mutable_reusablesnapshotartifact();
                artifact->set_storagebackend(pending.storageBackend);
                artifact->set_objectkey(pending.artifactObjectKey);
                artifact->set_size(response.snapshotinfo().size());
                artifact->set_sha256(response.snapshotinfo().sha256());
                artifact->set_format("sandboxd-checkpoint");
                artifact->set_formatversion(1);
                pending.completed = true;
                pending.completedResponse = response;
                Send(pending.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
                return;
            }
            Send(pending.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
            // Without exact reusable physical facts, keep the Master record
            // PUBLISHING and forget this dead in-memory request so a retry can
            // deterministically re-run checkpoint/inspection.
            snapshotRequests_.erase(iter);
            return;
        }
        Send(pending.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
        snapshotRequests_.erase(iter);
        return;
    }

    if (response.snapshotinfo().checkpointid() != pending.request.snapshotid()) {
        if (isPauseResume) {
            CompletePauseSnapshotError(requestID, static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                                       "runtime checkpoint response identity is invalid", false);
            return;
        }
        response.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
        response.set_message("runtime checkpoint response identity is invalid");
        response.clear_agentrequestgeneration();
        Send(pending.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
        snapshotRequests_.erase(iter);
        return;
    }

    const auto committed = localSnapshotStore_->Commit(pending.localCommitRequest);
    if (committed.status.IsError()) {
        response.set_code(static_cast<int32_t>(committed.status.StatusCode()));
        response.set_message(committed.status.RawMessage());
        response.clear_agentrequestgeneration();
        Send(pending.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
        snapshotRequests_.erase(iter);
        return;
    }
    CopyLocalSnapshotMetadata(committed.descriptor, pending.localSnapshot);
    auto *snapshotInfo = response.mutable_snapshotinfo();
    snapshotInfo->set_checkpointid(committed.descriptor.snapshotID);
    snapshotInfo->set_size(static_cast<int64_t>(committed.descriptor.size));
    snapshotInfo->set_sha256(committed.descriptor.sha256);
    ContinueSnapshotAfterLocalCommit(requestID, std::move(response));
}

void AgentServiceActor::ContinueSnapshotAfterLocalCommit(
    const std::string &requestID, ::messages::SnapshotRuntimeResponse response)
{
    auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        return;
    }
    auto &pending = iter->second;
    response.mutable_localsnapshot()->CopyFrom(pending.localSnapshot);
    const bool isPauseResume = pending.request.type() == common::PAUSE_RESUME;
    const bool isReusableSnapshot = pending.request.type() == common::SNAPSHOT;
    if (!isPauseResume && !isReusableSnapshot) {
        response.clear_agentrequestgeneration();
        Send(pending.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
        snapshotRequests_.erase(iter);
        return;
    }
    if (isReusableSnapshot) {
        // Preserve the committed local checkpoint facts while immutable
        // publication is in flight so exact cleanup remains possible.
        pending.completedResponse = response;
    }
    const auto publishRequest = BuildManagedSnapshotPublishRequest(
        pending.request, pending.artifactPath, pending.artifactObjectKey,
        pending.createdAtUnixSeconds, response.snapshotinfo());
    auto publishFuture = PublishManagedSnapshotArtifact(
        snapshotStorage_, snapshotWorker_, publishRequest);
    if (isPauseResume) {
        publishFuture.OnComplete(litebus::Defer(
            GetAID(), &AgentServiceActor::OnPauseArtifactPublished,
            requestID, std::placeholders::_1));
    } else {
        publishFuture.OnComplete(litebus::Defer(
            GetAID(), &AgentServiceActor::OnReusableArtifactPublished,
            requestID, std::placeholders::_1));
    }
}

void AgentServiceActor::ListLocalSnapshots(
    const litebus::AID &from, std::string &&, std::string &&msg)
{
    ::messages::ListLocalSnapshotsRequest request;
    if (!request.ParseFromString(msg) || request.requestid().empty()) {
        YRLOG_WARN("reject invalid ListLocalSnapshots request from {}", from.HashString());
        return;
    }
    if (from != localSchedFuncAgentMgrAID_) {
        YRLOG_WARN("{}|reject ListLocalSnapshots from untrusted sender {}",
                   request.requestid(), from.HashString());
        return;
    }
    ::messages::ListLocalSnapshotsResponse response;
    response.set_requestid(request.requestid());
    if (localSnapshotStore_ == nullptr) {
        response.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
        response.set_message("local snapshot store is unavailable");
    } else {
        response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        uint64_t totalBytes = 0;
        for (const auto &descriptor : localSnapshotStore_->List()) {
            totalBytes += descriptor.size;
            CopyLocalSnapshotMetadata(descriptor, *response.add_snapshots());
        }
        YRLOG_INFO("{}|listed {} committed local snapshots using {} bytes",
                   request.requestid(), response.snapshots_size(), totalBytes);
    }
    Send(from, "ListLocalSnapshotsResponse", response.SerializeAsString());
}

void AgentServiceActor::DeleteLocalSnapshot(
    const litebus::AID &from, std::string &&, std::string &&msg)
{
    ::messages::DeleteLocalSnapshotRequest request;
    if (!request.ParseFromString(msg) || request.requestid().empty()) {
        YRLOG_WARN("reject invalid DeleteLocalSnapshot request from {}", from.HashString());
        return;
    }
    if (from != localSchedFuncAgentMgrAID_) {
        YRLOG_WARN("{}|reject DeleteLocalSnapshot from untrusted sender {}",
                   request.requestid(), from.HashString());
        return;
    }
    ::messages::DeleteLocalSnapshotResponse response;
    response.set_requestid(request.requestid());
    Status status(StatusCode::ERR_PARAM_INVALID, "local snapshot store is unavailable");
    if (localSnapshotStore_ != nullptr) {
        LocalSnapshotDeleteIdentity identity;
        identity.snapshotID = request.snapshotid();
        identity.expectedGeneration = request.expectedgeneration();
        identity.expectedSize = request.expectedsize();
        identity.expectedSha256 = request.expectedsha256();
        status = localSnapshotStore_->Delete(identity);
    }
    response.set_code(static_cast<int32_t>(status.StatusCode()));
    response.set_message(status.IsError() ? status.RawMessage() : "");
    Send(from, "DeleteLocalSnapshotResponse", response.SerializeAsString());
}

void AgentServiceActor::OnReusableArtifactPublished(
    const std::string &requestID,
    const litebus::Future<snapshot_storage::ArtifactPublishResult> &publishFuture)
{
    auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        return;
    }
    const auto completeError = [this, requestID](int32_t code, const std::string &message,
                                                 bool resultUnknown,
                                                 const snapshot_storage::SnapshotObjectMetadata *metadata) {
        auto pending = snapshotRequests_.find(requestID);
        if (pending == snapshotRequests_.end()) {
            return;
        }
        uint64_t size = metadata == nullptr ? 0 : metadata->size;
        std::string sha256 = metadata == nullptr ? "" : metadata->sha256;
        if ((size == 0 || sha256.empty()) && pending->second.completedResponse.has_snapshotinfo()) {
            const auto &runtimeSnapshot = pending->second.completedResponse.snapshotinfo();
            if (runtimeSnapshot.size() > 0 && !runtimeSnapshot.sha256().empty()) {
                size = static_cast<uint64_t>(runtimeSnapshot.size());
                sha256 = runtimeSnapshot.sha256();
            }
        }

        ::messages::SnapshotRuntimeResponse response;
        response.set_requestid(pending->second.request.requestid());
        response.set_code(code);
        response.set_message(message);
        response.set_resultunknown(resultUnknown);
        response.mutable_localsnapshot()->CopyFrom(pending->second.localSnapshot);
        const bool exactArtifact = size > 0 && !sha256.empty()
            && !pending->second.storageBackend.empty()
            && !pending->second.artifactObjectKey.empty();
        if (exactArtifact) {
            auto *artifact = response.mutable_reusablesnapshotartifact();
            artifact->set_storagebackend(pending->second.storageBackend);
            artifact->set_objectkey(pending->second.artifactObjectKey);
            artifact->set_size(static_cast<int64_t>(size));
            artifact->set_sha256(sha256);
            artifact->set_format("sandboxd-checkpoint");
            artifact->set_formatversion(1);
            pending->second.completed = true;
            pending->second.completedResponse = response;
        }
        const auto caller = pending->second.caller;
        Send(caller, "SnapshotRuntimeResponse", response.SerializeAsString());
        if (!exactArtifact) {
            // Keep the Master PUBLISHING record, but forget this dead local
            // in-flight entry so the same deterministic request can replay
            // checkpoint/inspection instead of hanging forever. No physical
            // artifact is deleted without exact facts.
            snapshotRequests_.erase(pending);
        }
    };
    if (publishFuture.IsError()) {
        completeError(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
                      "reusable snapshot artifact publisher future failed", true, nullptr);
        return;
    }
    const auto &result = publishFuture.Get();
    if (result.status.IsError()) {
        completeError(static_cast<int32_t>(result.status.StatusCode()),
                      result.status.RawMessage(), result.resultUnknown, &result.metadata);
        return;
    }
    CompleteReusableSnapshot(requestID, result.metadata);
}

void AgentServiceActor::OnPauseArtifactPublished(
    const std::string &requestID,
    const litebus::Future<snapshot_storage::ArtifactPublishResult> &publishFuture)
{
    if (snapshotRequests_.find(requestID) == snapshotRequests_.end()) {
        return;
    }
    if (publishFuture.IsError()) {
        CompletePauseSnapshotError(requestID, static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
                                   "snapshot artifact publisher future failed", true);
        return;
    }
    const auto &result = publishFuture.Get();
    if (result.status.IsError()) {
        CompletePauseSnapshotError(requestID, static_cast<int32_t>(result.status.StatusCode()),
                                   result.status.RawMessage(), result.resultUnknown);
        return;
    }
    CompletePauseSnapshot(requestID, result.metadata);
}

void AgentServiceActor::CompletePauseSnapshot(
    const std::string &requestID, const snapshot_storage::SnapshotObjectMetadata &metadata)
{
    auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        return;
    }
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(iter->second.request.requestid());
    response.clear_agentrequestgeneration();
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("");
    response.set_resultunknown(false);
    auto *snapshot = response.mutable_snapshotinfo();
    snapshot->set_checkpointid(iter->second.request.snapshotid());
    snapshot->set_storage(iter->second.storageBackend);
    snapshot->set_size(static_cast<int64_t>(metadata.size));
    snapshot->set_sha256(metadata.sha256);
    snapshot->set_createtime(std::to_string(iter->second.localSnapshot.createdatunixseconds()));
    snapshot->set_ttlseconds(iter->second.request.ttl());
    snapshot->set_status(resources::SNAPSHOT_READY);
    response.mutable_localsnapshot()->CopyFrom(iter->second.localSnapshot);
    response.clear_physicalfact();
    iter->second.completed = true;
    iter->second.completedResponse = response;
    Send(iter->second.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
}

void AgentServiceActor::CompleteReusableSnapshot(
    const std::string &requestID, const snapshot_storage::SnapshotObjectMetadata &metadata)
{
    auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        return;
    }
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(iter->second.request.requestid());
    response.clear_agentrequestgeneration();
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("");
    response.set_resultunknown(false);
    auto *snapshot = response.mutable_snapshotinfo();
    snapshot->set_checkpointid(iter->second.request.snapshotid());
    snapshot->set_storage(iter->second.storageBackend);
    snapshot->set_size(static_cast<int64_t>(metadata.size));
    snapshot->set_sha256(metadata.sha256);
    snapshot->set_createtime(std::to_string(iter->second.localSnapshot.createdatunixseconds()));
    snapshot->set_ttlseconds(0);
    snapshot->set_status(resources::SNAPSHOT_READY);
    auto *artifact = response.mutable_reusablesnapshotartifact();
    artifact->set_storagebackend(iter->second.storageBackend);
    artifact->set_objectkey(iter->second.artifactObjectKey);
    artifact->set_size(static_cast<int64_t>(metadata.size));
    artifact->set_sha256(metadata.sha256);
    artifact->set_format("sandboxd-checkpoint");
    artifact->set_formatversion(1);
    response.mutable_localsnapshot()->CopyFrom(iter->second.localSnapshot);
    response.clear_physicalfact();
    iter->second.completed = true;
    iter->second.completedResponse = response;
    Send(iter->second.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
}

void AgentServiceActor::CompletePauseSnapshotError(const std::string &requestID, int32_t code,
                                                   const std::string &message, bool resultUnknown)
{
    auto iter = snapshotRequests_.find(requestID);
    if (iter == snapshotRequests_.end()) {
        return;
    }
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(iter->second.request.requestid());
    response.clear_agentrequestgeneration();
    response.set_code(code);
    response.set_message(message);
    response.set_resultunknown(resultUnknown);
    if (resultUnknown) {
        response.clear_physicalfact();
    }
    Send(iter->second.caller, "SnapshotRuntimeResponse", response.SerializeAsString());
    snapshotRequests_.erase(iter);
}

void AgentServiceActor::ForgetCompletedPauseResult(
    const ::messages::SnapshotAttemptFinalizeRequest &request)
{
    auto iter = snapshotRequests_.find(request.attemptid());
    if (iter == snapshotRequests_.end() || !iter->second.completed
        || iter->second.request.type() != common::PAUSE_RESUME
        || iter->second.request.instanceid() != request.instanceid()
        || iter->second.request.snapshotid() != request.snapshotid()) {
        return;
    }
    snapshotRequests_.erase(iter);
}

void AgentServiceActor::SnapshotAttemptFinalize(
    const litebus::AID &from, std::string &&, std::string &&msg)
{
    ::messages::SnapshotAttemptFinalizeRequest request;
    if (!request.ParseFromString(msg)) {
        return;
    }
    if (from != localSchedFuncAgentMgrAID_) {
        YRLOG_WARN("{}|reject snapshot attempt finalize from untrusted sender {}",
                   request.attemptid(), std::string(from));
        return;
    }
    const bool pausedDelete = request.operation() == ::messages::PAUSED_DELETED;
    const bool resumeFinalize = request.operation() == ::messages::RESUME_COMMITTED
        || request.operation() == ::messages::RESUME_ABORTED;
    const bool reusableFinalize = request.operation() == ::messages::REUSABLE_SNAPSHOT_COMMITTED
        || request.operation() == ::messages::REUSABLE_SNAPSHOT_ABORTED;
    if (request.protocolversion() != SNAPSHOT_ATTEMPT_PROTOCOL_VERSION
        || (request.operation() != ::messages::PAUSE_COMMITTED
            && request.operation() != ::messages::PAUSE_ABORTED
            && request.operation() != ::messages::RESUME_COMMITTED
            && request.operation() != ::messages::RESUME_ABORTED
            && request.operation() != ::messages::REUSABLE_SNAPSHOT_COMMITTED
            && request.operation() != ::messages::REUSABLE_SNAPSHOT_ABORTED
            && !pausedDelete)
        || request.tenantid().empty() || request.instanceid().empty()
        || request.snapshotid().empty() || request.attemptid().empty()
        || snapshotStorage_ == nullptr
        || ((resumeFinalize || reusableFinalize)
            && (snapshotWorker_ == nullptr || checkpointRoot_.empty()))) {
        SendSnapshotAttemptFinalizeResponse(from, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                                            "invalid snapshot attempt finalize request", false, false, false);
        return;
    }
    if (reusableFinalize) {
        std::string actualBackend;
        const auto backend = snapshot_storage::ResolveStorageBackend(
            snapshotStorage_, request.expectedstorage(), actualBackend);
        const auto pending = snapshotRequests_.find(request.attemptid());
        const bool pendingMismatch = pending != snapshotRequests_.end()
            && (!pending->second.completed || pending->second.request.type() != common::SNAPSHOT
                || pending->second.request.instanceid() != request.instanceid()
                || pending->second.request.runtimeid() != request.runtimeid()
                || pending->second.request.snapshotid() != request.snapshotid()
                || !pending->second.completedResponse.has_reusablesnapshotartifact()
                || pending->second.completedResponse.reusablesnapshotartifact().size()
                    != static_cast<int64_t>(request.expectedsize())
                || pending->second.completedResponse.reusablesnapshotartifact().sha256()
                    != request.expectedsha256()
                || pending->second.completedResponse.reusablesnapshotartifact().storagebackend()
                    != request.expectedstorage());
        if (backend.IsError() || actualBackend != request.expectedstorage()
            || request.expectedsize() == 0
            || request.expectedsha256().empty() || pendingMismatch
            || localSnapshotStore_ == nullptr) {
            SendSnapshotAttemptFinalizeResponse(
                from, request.attemptid(), static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                backend.IsError() ? backend.GetMessage()
                                  : "reusable snapshot cleanup identity or dependency is invalid",
                false, false, false);
            return;
        }
        LocalSnapshotDescriptor descriptor;
        auto localCleanup = localSnapshotStore_->ValidateForRestore(request.snapshotid(), descriptor);
        if (localCleanup.StatusCode() == StatusCode::FILE_NOT_FOUND) {
            localCleanup = Status::OK();
        } else if (localCleanup.IsOk()) {
            if (descriptor.anonymous || descriptor.size != request.expectedsize()
                || descriptor.sha256 != request.expectedsha256()) {
                localCleanup = snapshot_storage::detail::Conflict(
                    "reusable local snapshot cleanup identity does not match committed metadata");
            } else {
                localCleanup = localSnapshotStore_->Delete({descriptor.snapshotID, descriptor.generation,
                                                            descriptor.size, descriptor.sha256});
            }
        }
        if (localCleanup.IsError()) {
            CompleteReusableSnapshotFinalize(
                from, request, static_cast<int32_t>(localCleanup.StatusCode()),
                localCleanup.RawMessage(), false, false, false, false);
            return;
        }
        const auto temporaryKey = snapshot_storage::BuildReusableSnapshotTemporaryKey(
            snapshot_storage::StableTenantHash(request.tenantid()),
            request.snapshotid(), request.attemptid());
        snapshotStorage_->Delete(temporaryKey)
            .OnComplete(litebus::Defer(
                GetAID(), &AgentServiceActor::OnReusableAttemptTemporaryDeleted,
                from, request, std::placeholders::_1));
        return;
    }
    if (pausedDelete) {
        if (request.snapshotid().empty()) {
            SendSnapshotAttemptFinalizeResponse(
                from, request.attemptid(), static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                "paused snapshot cleanup requires exact immutable identity", false, true, false);
            return;
        }
        std::string actualBackend;
        const auto backend = snapshot_storage::ResolveStorageBackend(
            snapshotStorage_, request.expectedstorage(), actualBackend);
        if (backend.IsError() || actualBackend != request.expectedstorage()
            || request.expectedsize() == 0 || request.expectedsha256().empty()
            || localSnapshotStore_ == nullptr) {
            SendSnapshotAttemptFinalizeResponse(
                from, request.attemptid(), static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                backend.IsError() ? backend.GetMessage()
                                  : "paused snapshot cleanup identity or dependency is invalid",
                false, false, false);
            return;
        }
        const auto finalKey = snapshot_storage::BuildPauseSnapshotKey(
            snapshot_storage::StableTenantHash(request.tenantid()),
            request.instanceid(), request.snapshotid());
        snapshotStorage_->Stat(finalKey).OnComplete(
            litebus::Defer(GetAID(), &AgentServiceActor::OnPausedSnapshotDeleteProbed,
                           from, request, finalKey, std::placeholders::_1));
        return;
    }
    if (request.operation() == ::messages::RESUME_COMMITTED) {
        std::string actualBackend;
        const auto backend = snapshot_storage::ResolveStorageBackend(
            snapshotStorage_, request.expectedstorage(), actualBackend);
        if (backend.IsError() || actualBackend != request.expectedstorage()
            || request.expectedsize() == 0 || request.expectedsha256().empty()) {
            SendSnapshotAttemptFinalizeResponse(
                from, request.attemptid(), static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                backend.IsError() ? backend.GetMessage() : "resume snapshot cleanup metadata is invalid",
                false, false, false);
            return;
        }
    }
    if (request.operation() == ::messages::RESUME_ABORTED) {
        const auto runtimeID = resume_identity::RuntimeID(request.instanceid(), request.attemptid());
        if ((!request.runtimeid().empty() && request.runtimeid() != runtimeID)
            || !registerRuntimeMgr_.registered || !isRegisterCompleted_) {
            SendSnapshotAttemptFinalizeResponse(
                from, request.attemptid(),
                static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID),
                "resume loser cleanup runtime identity or RuntimeManager registration is invalid",
                false, false, false);
            return;
        }
        const auto cleanupRequestID = "resume-release/" + request.instanceid()
            + "/resume-target/" + request.attemptid();
        resumeAbortFinalizations_[cleanupRequestID] = { from, request, runtimeID };
        messages::StopInstanceRequest stop;
        stop.set_runtimeid(runtimeID);
        stop.set_requestid(cleanupRequestID);
        stop.set_traceid("resume-abort-" + request.attemptid());
        stop.set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SANDBOXD));
        Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address),
             "StopInstance", stop.SerializeAsString());
        return;
    }
    if (request.operation() == ::messages::RESUME_COMMITTED) {
        OnResumeAttemptLocalFinalized(
            from, request, litebus::Future<Status>(Status::OK()));
        return;
    }
    const auto tenantHash = snapshot_storage::StableTenantHash(request.tenantid());
    const auto temporaryKey = snapshot_storage::BuildPauseSnapshotTemporaryKey(
        tenantHash, request.instanceid(), request.snapshotid(), request.attemptid());
    snapshotStorage_->Delete(temporaryKey)
        .OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::OnPauseAttemptTemporaryDeleted,
                                   from, request, std::placeholders::_1));
}

void AgentServiceActor::OnPausedSnapshotDeleteProbed(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const std::string &finalKey,
    const litebus::Future<snapshot_storage::SnapshotStat> &future)
{
    if (future.IsError()) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
            "paused snapshot cleanup Stat future failed", true, false, false);
        return;
    }
    const auto &stat = future.Get();
    const bool remoteMissing = stat.status.StatusCode() == StatusCode::FILE_NOT_FOUND;
    if (!remoteMissing
        && (stat.status.IsError() || !stat.metadata.complete
            || stat.metadata.snapshotID != request.snapshotid()
            || stat.metadata.size != request.expectedsize()
            || stat.metadata.sha256 != request.expectedsha256())) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(), static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED),
            "paused snapshot cleanup metadata does not match immutable object", false, false, false);
        return;
    }
    LocalSnapshotDescriptor descriptor;
    auto localCleanup = localSnapshotStore_->ValidateForRestore(request.snapshotid(), descriptor);
    if (localCleanup.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        localCleanup = Status::OK();
    } else if (localCleanup.IsOk()) {
        if (!descriptor.anonymous || descriptor.size != request.expectedsize()
            || descriptor.sha256 != request.expectedsha256()) {
            localCleanup = snapshot_storage::detail::Conflict(
                "paused local snapshot cleanup identity does not match committed metadata");
        } else {
            localCleanup = localSnapshotStore_->Delete({descriptor.snapshotID, descriptor.generation,
                                                        descriptor.size, descriptor.sha256});
        }
    }
    if (localCleanup.IsError()) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(), static_cast<int32_t>(localCleanup.StatusCode()),
            localCleanup.RawMessage(), false, false, false);
        return;
    }
    if (remoteMissing) {
        SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::SUCCESS),
                                            "", false, true, true);
        return;
    }
    snapshotStorage_->Delete(finalKey).OnComplete(
        litebus::Defer(GetAID(), &AgentServiceActor::OnPausedSnapshotDeleted,
                       caller, request, std::placeholders::_1));
}

void AgentServiceActor::OnPausedSnapshotDeleted(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND)) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(),
            future.IsError() ? static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION)
                             : static_cast<int32_t>(future.Get().StatusCode()),
            future.IsError() ? "paused snapshot cleanup Delete future failed" : future.Get().ToString(),
            future.IsError(), true, false);
        return;
    }
    SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                        static_cast<int32_t>(StatusCode::SUCCESS),
                                        "", false, true, true);
}

void AgentServiceActor::OnResumeAttemptLocalFinalized(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || future.Get().IsError()) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(),
            future.IsError() ? static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION)
                             : static_cast<int32_t>(future.Get().StatusCode()),
            future.IsError() ? "local resume attempt cleanup future failed" : future.Get().ToString(),
            future.IsError(), false, false);
        return;
    }
    if (request.operation() == ::messages::RESUME_ABORTED) {
        SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::SUCCESS),
                                            "", false, true, false);
        return;
    }
    const auto finalKey = snapshot_storage::BuildPauseSnapshotKey(
        snapshot_storage::StableTenantHash(request.tenantid()),
        request.instanceid(), request.snapshotid());
    snapshotStorage_->Stat(finalKey).OnComplete(
        litebus::Defer(GetAID(), &AgentServiceActor::OnResumeSnapshotDeleteProbed,
                       caller, request, finalKey, std::placeholders::_1));
}

void AgentServiceActor::OnResumeSnapshotDeleteProbed(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const std::string &finalKey,
    const litebus::Future<snapshot_storage::SnapshotStat> &future)
{
    if (future.IsError()) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
            "resume snapshot cleanup Stat future failed", true, true, false);
        return;
    }
    const auto &stat = future.Get();
    if (stat.status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::SUCCESS),
                                            "", false, true, true);
        return;
    }
    if (stat.status.IsError() || !stat.metadata.complete
        || stat.metadata.snapshotID != request.snapshotid()
        || stat.metadata.size != request.expectedsize()
        || stat.metadata.sha256 != request.expectedsha256()) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(), static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED),
            "resume snapshot cleanup metadata does not match immutable object", false, true, false);
        return;
    }
    snapshotStorage_->Delete(finalKey).OnComplete(
        litebus::Defer(GetAID(), &AgentServiceActor::OnResumeSnapshotDeleted,
                       caller, request, std::placeholders::_1));
}

void AgentServiceActor::OnResumeSnapshotDeleted(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND)) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(),
            future.IsError() ? static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION)
                             : static_cast<int32_t>(future.Get().StatusCode()),
            future.IsError() ? "resume snapshot cleanup future failed" : future.Get().ToString(),
            future.IsError(), true, false);
        return;
    }
    SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                        static_cast<int32_t>(StatusCode::SUCCESS),
                                        "", false, true, true);
}

void AgentServiceActor::OnReusableAttemptTemporaryDeleted(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND)) {
        CompleteReusableSnapshotFinalize(
            caller, request,
            future.IsError() ? static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION)
                             : static_cast<int32_t>(future.Get().StatusCode()),
            future.IsError() ? "reusable snapshot staging cleanup future failed" : future.Get().ToString(),
            future.IsError(), true, false, false);
        return;
    }
    if (request.operation() == ::messages::REUSABLE_SNAPSHOT_COMMITTED) {
        CompleteReusableSnapshotFinalize(caller, request,
                                         static_cast<int32_t>(StatusCode::SUCCESS),
                                         "", false, true, true, true);
        return;
    }
    const auto finalKey = snapshot_storage::BuildReusableSnapshotKey(
        snapshot_storage::StableTenantHash(request.tenantid()),
        request.snapshotid());
    snapshotStorage_->Stat(finalKey).OnComplete(
        litebus::Defer(GetAID(), &AgentServiceActor::OnReusableSnapshotDeleteProbed,
                       caller, request, finalKey, std::placeholders::_1));
}

void AgentServiceActor::OnReusableSnapshotDeleteProbed(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const std::string &finalKey,
    const litebus::Future<snapshot_storage::SnapshotStat> &future)
{
    if (future.IsError()) {
        CompleteReusableSnapshotFinalize(
            caller, request, static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
            "reusable snapshot final Stat future failed", true, true, false, false);
        return;
    }
    const auto &stat = future.Get();
    if (stat.status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        CompleteReusableSnapshotFinalize(caller, request,
                                         static_cast<int32_t>(StatusCode::SUCCESS),
                                         "", false, true, true, true);
        return;
    }
    if (stat.status.IsError() || !stat.metadata.complete
        || stat.metadata.snapshotID != request.snapshotid()
        || stat.metadata.size != request.expectedsize()
        || stat.metadata.sha256 != request.expectedsha256()) {
        CompleteReusableSnapshotFinalize(
            caller, request, static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED),
            "reusable snapshot final object does not match aborted attempt",
            false, true, false, false);
        return;
    }
    snapshotStorage_->Delete(finalKey).OnComplete(
        litebus::Defer(GetAID(), &AgentServiceActor::OnReusableSnapshotDeleted,
                       caller, request, std::placeholders::_1));
}

void AgentServiceActor::OnReusableSnapshotDeleted(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND)) {
        CompleteReusableSnapshotFinalize(
            caller, request,
            future.IsError() ? static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION)
                             : static_cast<int32_t>(future.Get().StatusCode()),
            future.IsError() ? "reusable snapshot cleanup future failed" : future.Get().ToString(),
            future.IsError(), true, false, false);
        return;
    }
    CompleteReusableSnapshotFinalize(caller, request,
                                     static_cast<int32_t>(StatusCode::SUCCESS),
                                     "", false, true, true, true);
}

void AgentServiceActor::DeleteReusableSnapshotArtifact(
    const litebus::AID &from, std::string &&, std::string &&msg)
{
    ::messages::DeleteReusableSnapshotArtifactRequest request;
    if (!request.ParseFromString(msg)) {
        SendDeleteReusableSnapshotArtifactResponse(
            from, "", common::ERR_PARAM_INVALID,
            "failed to parse reusable Snapshot artifact delete request");
        return;
    }
    if (from != localSchedFuncAgentMgrAID_) {
        YRLOG_WARN("{}|reject reusable Snapshot artifact delete from untrusted sender {}",
                   request.requestid(), std::string(from));
        return;
    }

    const auto &artifact = request.artifact();
    std::string actualBackend;
    const auto backend = snapshot_storage::ResolveStorageBackend(
        snapshotStorage_, snapshotStorageBackend_, actualBackend);
    const auto canonicalKey = snapshot_storage::BuildReusableSnapshotKey(
        snapshot_storage::StableTenantHash(request.tenantid()),
        request.snapshotid());
    const bool validSha256 = artifact.sha256().size() == 64
        && std::all_of(artifact.sha256().begin(), artifact.sha256().end(), [](unsigned char value) {
            return std::isxdigit(value) != 0;
        });
    if (request.requestid().empty() || request.tenantid().empty()
        || request.snapshotid().empty() || snapshotStorage_ == nullptr
        || backend.IsError() || actualBackend != artifact.storagebackend()
        || artifact.objectkey() != canonicalKey || artifact.size() <= 0
        || !validSha256 || artifact.format() != "sandboxd-checkpoint"
        || artifact.formatversion() != 1) {
        SendDeleteReusableSnapshotArtifactResponse(
            from, request.requestid(), common::ERR_PARAM_INVALID,
            backend.IsError() ? backend.GetMessage()
                              : "reusable Snapshot artifact delete identity is not canonical and frozen");
        return;
    }

    if (localSnapshotStore_ != nullptr) {
        LocalSnapshotDescriptor descriptor;
        auto localStatus = localSnapshotStore_->ValidateForRestore(request.snapshotid(), descriptor);
        if (localStatus.IsOk() && !descriptor.anonymous
            && descriptor.size == static_cast<uint64_t>(artifact.size())
            && descriptor.sha256 == artifact.sha256()) {
            localStatus = localSnapshotStore_->Delete({descriptor.snapshotID, descriptor.generation,
                                                       descriptor.size, descriptor.sha256});
        }
        if (localStatus.IsError() && localStatus.StatusCode() != StatusCode::FILE_NOT_FOUND) {
            YRLOG_WARN("{}|reusable Snapshot({}) local cache cleanup is best-effort: {}",
                       request.requestid(), request.snapshotid(), localStatus.ToString());
        }
    }

    snapshotStorage_->Delete(canonicalKey).OnComplete(
        litebus::Defer(GetAID(), &AgentServiceActor::OnReusableSnapshotArtifactDeleted,
                       from, request, std::placeholders::_1));
}

void AgentServiceActor::OnReusableSnapshotArtifactDeleted(
    const litebus::AID &caller,
    const ::messages::DeleteReusableSnapshotArtifactRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError()) {
        SendDeleteReusableSnapshotArtifactResponse(
            caller, request.requestid(), common::ERR_INNER_COMMUNICATION,
            "reusable Snapshot artifact Delete future failed");
        return;
    }
    if (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND
        && future.Get().StatusCode() != StatusCode::BP_DATASYSTEM_ERROR) {
        SendDeleteReusableSnapshotArtifactResponse(
            caller, request.requestid(),
            static_cast<int32_t>(Status::GetPosixErrorCode(future.Get().StatusCode())),
            future.Get().ToString());
        return;
    }
    if (future.Get().StatusCode() == StatusCode::BP_DATASYSTEM_ERROR) {
        YRLOG_WARN("{}|reusable Snapshot({}) DataSystem artifact cleanup is best-effort: {}",
                   request.requestid(), request.snapshotid(), future.Get().ToString());
    }
    SendDeleteReusableSnapshotArtifactResponse(
        caller, request.requestid(), common::ERR_NONE, "");
}

void AgentServiceActor::SendDeleteReusableSnapshotArtifactResponse(
    const litebus::AID &caller, const std::string &requestID,
    int32_t code, const std::string &message)
{
    ::messages::DeleteReusableSnapshotArtifactResponse response;
    response.set_requestid(requestID);
    response.set_code(code);
    response.set_message(message);
    (void)Send(caller, "DeleteReusableSnapshotArtifactResponse", response.SerializeAsString());
}

void AgentServiceActor::CompleteReusableSnapshotFinalize(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    int32_t code, const std::string &message, bool resultUnknown,
    bool localComplete, bool remoteComplete, bool forgetSnapshot)
{
    if (forgetSnapshot) {
        const auto pending = snapshotRequests_.find(request.attemptid());
        if (pending != snapshotRequests_.end()
            && pending->second.request.type() == common::SNAPSHOT
            && pending->second.request.instanceid() == request.instanceid()
            && pending->second.request.snapshotid() == request.snapshotid()) {
            snapshotRequests_.erase(pending);
        }
    }
    SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(), code, message,
                                        resultUnknown, localComplete, remoteComplete);
}

void AgentServiceActor::OnPauseAttemptTemporaryDeleted(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND)) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(),
            future.IsError() ? static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION)
                                     : static_cast<int32_t>(future.Get().StatusCode()),
            future.IsError() ? "pause staging cleanup future failed" : future.Get().ToString(),
            future.IsError(), true, false);
        return;
    }
    if (request.operation() == ::messages::PAUSE_COMMITTED) {
        ForgetCompletedPauseResult(request);
        SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::SUCCESS),
                                            "", false, true, true);
        return;
    }
    const auto finalKey = snapshot_storage::BuildPauseSnapshotKey(
        snapshot_storage::StableTenantHash(request.tenantid()),
        request.instanceid(), request.snapshotid());
    snapshotStorage_->Stat(finalKey)
        .OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::OnPauseAttemptFinalProbed,
                                   caller, request, std::placeholders::_1));
}

void AgentServiceActor::OnPauseAttemptFinalProbed(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<snapshot_storage::SnapshotStat> &future)
{
    if (future.IsError()) {
        SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
                                            "pause final Stat future failed", true, true, false);
        return;
    }
    const auto &stat = future.Get();
    if (stat.status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        ForgetCompletedPauseResult(request);
        SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                            static_cast<int32_t>(StatusCode::SUCCESS),
                                            "", false, true, true);
        return;
    }
    if (stat.status.IsError() || !stat.metadata.complete
        || stat.metadata.snapshotID != request.snapshotid()
        || stat.metadata.size != request.expectedsize()
        || stat.metadata.sha256 != request.expectedsha256()) {
        SendSnapshotAttemptFinalizeResponse(
            caller, request.attemptid(), static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED),
            "pause final object does not match aborted attempt", false, true, false);
        return;
    }
    const auto finalKey = snapshot_storage::BuildPauseSnapshotKey(
        snapshot_storage::StableTenantHash(request.tenantid()),
        request.instanceid(), request.snapshotid());
    snapshotStorage_->Delete(finalKey)
        .OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::OnPauseAttemptFinalDeleted,
                                   caller, request, std::placeholders::_1));
}

void AgentServiceActor::OnPauseAttemptFinalDeleted(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request,
    const litebus::Future<Status> &future)
{
    if (future.IsError() || (future.Get().IsError()
        && future.Get().StatusCode() != StatusCode::FILE_NOT_FOUND)) {
        constexpr uint32_t retryIntervalMs = 10;
        (void)litebus::AsyncAfter(retryIntervalMs, GetAID(),
                                  &AgentServiceActor::RetryPauseAttemptFinalDelete,
                                  caller, request);
        return;
    }
    ForgetCompletedPauseResult(request);
    SendSnapshotAttemptFinalizeResponse(caller, request.attemptid(),
                                        static_cast<int32_t>(StatusCode::SUCCESS),
                                        "", false, true, true);
}

void AgentServiceActor::RetryPauseAttemptFinalDelete(
    const litebus::AID &caller, const ::messages::SnapshotAttemptFinalizeRequest &request)
{
    const auto finalKey = snapshot_storage::BuildPauseSnapshotKey(
        snapshot_storage::StableTenantHash(request.tenantid()),
        request.instanceid(), request.snapshotid());
    snapshotStorage_->Delete(finalKey)
        .OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::OnPauseAttemptFinalDeleted,
                                   caller, request, std::placeholders::_1));
}

void AgentServiceActor::SendSnapshotAttemptFinalizeResponse(
    const litebus::AID &caller, const std::string &attemptID, int32_t code, const std::string &message,
    bool resultUnknown, bool localComplete, bool remoteComplete)
{
    ::messages::SnapshotAttemptFinalizeResponse response;
    response.set_code(code);
    response.set_message(message);
    response.set_resultunknown(resultUnknown);
    response.set_localcleanupcomplete(localComplete);
    response.set_remotecleanupcomplete(remoteComplete);
    response.set_attemptid(attemptID);
    Send(caller, "SnapshotAttemptFinalizeResponse", response.SerializeAsString());
}

}  // namespace functionsystem::function_agent
