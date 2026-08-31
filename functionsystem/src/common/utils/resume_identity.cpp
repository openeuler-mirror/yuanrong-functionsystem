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

#include "common/utils/resume_identity.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iomanip>
#include <sstream>

#include <openssl/sha.h>

#include "common/types/instance_state.h"

namespace functionsystem::resume_identity {
namespace {

constexpr char RETIRED_PAUSE_SOURCE_RUNTIME_ID_EXTENSION[] =
    "yr.internal.pause.source_runtime_id";
constexpr char RETIRED_PAUSE_TRUSTED_RUNTIME_ID_EXTENSION[] =
    "yr.internal.pause.trusted_runtime_id";

void AppendIdentityPart(std::string &identity, const std::string &part)
{
    identity.append(std::to_string(part.size()));
    identity.push_back(':');
    identity.append(part);
}

}  // namespace

bool IsResultUnknownStatusCode(int32_t code)
{
    const auto status = static_cast<StatusCode>(code);
    return status == StatusCode::GRPC_CANCELLED
        || status == StatusCode::GRPC_DEADLINE_EXCEEDED
        || status == StatusCode::GRPC_UNAVAILABLE;
}

bool IsResumeProtocolExtension(const std::string &key)
{
    return key.rfind(EXTENSION_PREFIX, 0) == 0;
}

bool IsReservedExtension(const std::string &key)
{
    return IsResumeProtocolExtension(key)
        || key == RETIRED_PAUSE_SOURCE_RUNTIME_ID_EXTENSION
        || key == RETIRED_PAUSE_TRUSTED_RUNTIME_ID_EXTENSION;
}

std::string Sha256Hex(const std::string &input)
{
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), digest.data());
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

bool ParsePositiveInt64(const std::string &value, int64_t *result)
{
    if (result == nullptr || value.empty()) {
        return false;
    }
    try {
        size_t parsed = 0;
        const auto number = std::stoll(value, &parsed);
        if (parsed != value.size() || number <= 0) {
            return false;
        }
        *result = number;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool IsCompleteReadySnapshot(const resources::SnapshotInfo &snapshot)
{
    int64_t createTime = 0;
    return snapshot.status() == resources::SNAPSHOT_READY && !snapshot.checkpointid().empty()
        && (snapshot.storage() == "obs" || snapshot.storage() == "datasystem"
            || snapshot.storage() == "local")
        && snapshot.ttlseconds() > 0
        && ParsePositiveInt64(snapshot.createtime(), &createTime);
}

bool ValidateReusableSnapshotRestore(const ::messages::ReusableSnapshotRestore &restore)
{
    const auto &artifact = restore.artifact();
    const bool validSnapshotID = !restore.snapshotid().empty()
        && restore.snapshotid() != "." && restore.snapshotid() != ".."
        && restore.snapshotid().find('/') == std::string::npos
        && restore.snapshotid().find('\\') == std::string::npos;
    const bool validObjectKey = !artifact.objectkey().empty()
        && artifact.objectkey().front() != '/'
        && artifact.objectkey().find("../") == std::string::npos
        && artifact.objectkey().find("/..") == std::string::npos;
    return restore.allowlogicalinstanceidrebind() && validSnapshotID
        && (artifact.storagebackend() == "obs" || artifact.storagebackend() == "datasystem"
            || artifact.storagebackend() == "local")
        && validObjectKey
        && (artifact.storagebackend() != "local" || !artifact.sourcenodeid().empty());
}

std::string IdentityDigest(const std::string &logicalInstanceID, const std::string &logicalRequestID,
                           const std::string &tenantID, const std::string &targetAttemptID,
                           const resources::SnapshotInfo &snapshot, int64_t expectedVersion,
                           const std::string &targetAgentID, int64_t protocolVersion)
{
    std::string identity;
    AppendIdentityPart(identity, logicalInstanceID);
    AppendIdentityPart(identity, logicalRequestID);
    AppendIdentityPart(identity, tenantID);
    AppendIdentityPart(identity, targetAttemptID);
    AppendIdentityPart(identity, std::to_string(expectedVersion));
    AppendIdentityPart(identity, targetAgentID);
    AppendIdentityPart(identity, std::to_string(protocolVersion));
    AppendIdentityPart(identity, snapshot.checkpointid());
    AppendIdentityPart(identity, snapshot.storage());
    AppendIdentityPart(identity, std::to_string(static_cast<int32_t>(snapshot.status())));
    return Sha256Hex(identity);
}

std::string ExecutorMarker(const std::string &logicalInstanceID, const std::string &logicalRequestID,
                           const std::string &tenantID, const std::string &targetAttemptID,
                           const resources::SnapshotInfo &snapshot, int64_t expectedVersion,
                           const std::string &targetAgentID, int64_t protocolVersion)
{
    std::string marker;
    AppendIdentityPart(marker, logicalRequestID);
    marker.append(IdentityDigest(logicalInstanceID, logicalRequestID, tenantID, targetAttemptID, snapshot,
                                 expectedVersion, targetAgentID, protocolVersion));
    return marker;
}

bool ParseAndValidateExecutorMarker(const std::string &logicalInstanceID, const std::string &tenantID,
                                    const std::string &targetAttemptID, const resources::SnapshotInfo &snapshot,
                                    int64_t expectedVersion, const std::string &targetAgentID,
                                    int64_t protocolVersion, const std::string &marker,
                                    std::string *logicalRequestID)
{
    const auto separator = marker.find(':');
    if (separator == std::string::npos || logicalRequestID == nullptr) {
        return false;
    }
    size_t logicalSize = 0;
    try {
        size_t parsed = 0;
        logicalSize = std::stoull(marker.substr(0, separator), &parsed);
        if (parsed != separator) {
            return false;
        }
    } catch (const std::exception &) {
        return false;
    }
    const auto logicalStart = separator + 1;
    if (logicalSize == 0 || logicalStart + logicalSize > marker.size()) {
        return false;
    }
    const auto logical = marker.substr(logicalStart, logicalSize);
    const auto expected = ExecutorMarker(logicalInstanceID, logical, tenantID, targetAttemptID, snapshot,
                                         expectedVersion, targetAgentID, protocolVersion);
    if (marker != expected) {
        return false;
    }
    *logicalRequestID = logical;
    return true;
}

std::string RuntimeID(const std::string &logicalInstanceID, const std::string &targetAttemptID)
{
    std::string identity;
    AppendIdentityPart(identity, logicalInstanceID);
    AppendIdentityPart(identity, targetAttemptID);
    // sandboxd and runsc both prefix the runtime ID when constructing the
    // AF_UNIX control socket path. A full 64-character digest overflows
    // sockaddr_un.sun_path under the default standalone sandbox root. Keep a
    // deterministic 160-bit identity; the full attempt and logical identity
    // remain authoritative labels on the sandbox physical fact.
    constexpr size_t runtimeDigestHexLength = 40;
    return std::string(RUNTIME_ID_PREFIX) + Sha256Hex(identity).substr(0, runtimeDigestHexLength);
}

bool IsExactResumeTargetCleanupRequest(const std::string &cleanupRequestID,
                                       const std::string &logicalInstanceID, const std::string &runtimeID)
{
    const auto prefix = "resume-release/" + logicalInstanceID + "/resume-target/";
    if (cleanupRequestID.rfind(prefix, 0) != 0) {
        return false;
    }
    const auto targetAttemptID = cleanupRequestID.substr(prefix.size());
    return !targetAttemptID.empty() && targetAttemptID.find('/') == std::string::npos
        && runtimeID == RuntimeID(logicalInstanceID, targetAttemptID);
}

bool ParseExactResumeTargetCleanupRequest(const std::string &cleanupRequestID, const std::string &runtimeID,
                                          std::string *logicalInstanceID, std::string *targetAttemptID)
{
    static const std::string prefix = "resume-release/";
    static const std::string separator = "/resume-target/";
    if (logicalInstanceID == nullptr || targetAttemptID == nullptr || cleanupRequestID.rfind(prefix, 0) != 0) {
        return false;
    }
    const auto separatorAt = cleanupRequestID.find(separator, prefix.size());
    if (separatorAt == std::string::npos
        || cleanupRequestID.find(separator, separatorAt + separator.size()) != std::string::npos) {
        return false;
    }
    const auto logical = cleanupRequestID.substr(prefix.size(), separatorAt - prefix.size());
    const auto attempt = cleanupRequestID.substr(separatorAt + separator.size());
    if (logical.empty() || attempt.empty() || logical.find('/') != std::string::npos
        || attempt.find('/') != std::string::npos || runtimeID != RuntimeID(logical, attempt)) {
        return false;
    }
    *logicalInstanceID = logical;
    *targetAttemptID = attempt;
    return true;
}

bool IsCommittedResumeWinner(const resources::InstanceInfo &authoritative, const std::string &logicalInstanceID,
                             const std::string &targetAttemptID)
{
    return !logicalInstanceID.empty() && !targetAttemptID.empty()
        && authoritative.instanceid() == logicalInstanceID
        && authoritative.instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING)
        && authoritative.runtimeid() == RuntimeID(logicalInstanceID, targetAttemptID)
        && !authoritative.functionproxyid().empty()
        && authoritative.functionproxyid() != INSTANCE_MANAGER_OWNER
        && !authoritative.functionagentid().empty()
        && (!authoritative.runtimeaddress().empty() || !authoritative.proxygrpcaddress().empty());
}

bool SnapshotIdentityMatches(const resources::SnapshotInfo &left, const resources::SnapshotInfo &right)
{
    return left.checkpointid() == right.checkpointid() && left.status() == right.status()
        && left.storage() == right.storage();
}

bool IsAuthoritativePausedControlIdentity(const resources::InstanceInfo &instance)
{
    return instance.functionproxyid() == INSTANCE_MANAGER_OWNER
        && instance.runtimeid().empty() && instance.runtimeaddress().empty()
        && instance.functionagentid().empty() && instance.containerid().empty()
        && instance.containerip().empty() && instance.unitid().empty()
        && instance.proxygrpcaddress().empty();
}

Status ValidateMasterSchedule(const messages::ScheduleRequest &request,
                              const resources::InstanceInfo &authoritative)
{
    const auto &extensions = request.instance().scheduleoption().extension();
    const auto marker = extensions.find(MASTER_MARKER_EXTENSION);
    if (request.requestid().empty() || marker == extensions.end() || marker->second != request.requestid()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "resume master marker is invalid");
    }
    for (const auto &[key, value] : extensions) {
        (void)value;
        if (IsReservedExtension(key) && key != MASTER_MARKER_EXTENSION) {
            return Status(StatusCode::ERR_PARAM_INVALID, "resume request contains an invalid reserved marker");
        }
    }
    if (HasReservedExtension(request.instance().extensions())) {
        return Status(StatusCode::ERR_PARAM_INVALID, "resume instance contains a reserved marker");
    }
    const auto &candidate = request.instance();
    if (candidate.instanceid() == authoritative.instanceid()
        && candidate.requestid() == authoritative.requestid()
        && candidate.tenantid() == authoritative.tenantid()
        && candidate.version() > 0 && candidate.version() + 1 == authoritative.version()
        && candidate.instancestatus().code() == static_cast<int32_t>(InstanceState::PAUSED)
        && candidate.has_snapshotinfo() && IsCompleteReadySnapshot(candidate.snapshotinfo())
        && candidate.functionproxyid().empty() && candidate.runtimeid().empty()
        && candidate.runtimeaddress().empty() && candidate.functionagentid().empty()
        && candidate.containerid().empty() && candidate.containerip().empty()
        && candidate.unitid().empty() && candidate.proxygrpcaddress().empty()
        && (!authoritative.has_snapshotinfo()
            || SnapshotIdentityMatches(candidate.snapshotinfo(), authoritative.snapshotinfo()))
        && IsCommittedResumeWinner(authoritative, candidate.instanceid(), request.requestid())) {
        return Status::OK();
    }
    if (authoritative.instanceid().empty() || authoritative.requestid().empty()
        || authoritative.tenantid().empty() || authoritative.version() <= 0
        || authoritative.instancestatus().code() != static_cast<int32_t>(InstanceState::PAUSED)
        || !IsAuthoritativePausedControlIdentity(authoritative)
        || !authoritative.has_snapshotinfo() || !IsCompleteReadySnapshot(authoritative.snapshotinfo())) {
        return Status(StatusCode::ERR_PARAM_INVALID, "authoritative paused identity is incomplete");
    }
    if (candidate.instanceid() != authoritative.instanceid()
        || candidate.requestid() != authoritative.requestid()
        || candidate.tenantid() != authoritative.tenantid()
        || candidate.version() != authoritative.version()
        || candidate.instancestatus().code() != static_cast<int32_t>(InstanceState::PAUSED)
        || !candidate.has_snapshotinfo()
        || !SnapshotIdentityMatches(candidate.snapshotinfo(), authoritative.snapshotinfo())) {
        return Status(StatusCode::ERR_PARAM_INVALID, "resume schedule does not match authoritative paused identity");
    }
    if (!candidate.functionproxyid().empty() || !candidate.runtimeid().empty()
        || !candidate.runtimeaddress().empty() || !candidate.functionagentid().empty()
        || !candidate.containerid().empty() || !candidate.containerip().empty()
        || !candidate.unitid().empty() || !candidate.proxygrpcaddress().empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "resume schedule carries source physical identity");
    }
    return Status::OK();
}

TrustedResumeIdentity TrustedResumeIdentity::FromSchedule(const messages::ScheduleRequest &request)
{
    TrustedResumeIdentity identity;
    identity.logicalInstanceID = request.instance().instanceid();
    identity.logicalRequestID = request.instance().requestid();
    identity.tenantID = request.instance().tenantid();
    identity.targetAttemptID = request.requestid();
    identity.snapshot = request.instance().snapshotinfo();
    identity.expectedVersion = request.instance().version();
    identity.targetAgentID = request.instance().functionagentid();
    return identity;
}

bool TrustedResumeIdentity::MatchesSchedule(const messages::ScheduleRequest &request) const
{
    return !logicalInstanceID.empty() && !logicalRequestID.empty() && !tenantID.empty()
        && !targetAttemptID.empty() && expectedVersion > 0 && !targetAgentID.empty()
        && protocolVersion == PROTOCOL_VERSION && IsCompleteReadySnapshot(snapshot)
        && request.requestid() == targetAttemptID
        && request.instance().instanceid() == logicalInstanceID
        && request.instance().requestid() == logicalRequestID
        && request.instance().tenantid() == tenantID
        && request.instance().functionagentid() == targetAgentID
        && request.instance().version() == expectedVersion
        && request.instance().instancestatus().code() == static_cast<int32_t>(InstanceState::PAUSED)
        && request.instance().has_snapshotinfo()
        && SnapshotIdentityMatches(request.instance().snapshotinfo(), snapshot);
}

bool TrustedResumeIdentity::MatchesAuthoritative(const resources::InstanceInfo &authoritative) const
{
    return !logicalInstanceID.empty() && !logicalRequestID.empty() && !tenantID.empty()
        && !targetAttemptID.empty() && expectedVersion > 0 && !targetAgentID.empty()
        && protocolVersion == PROTOCOL_VERSION && IsCompleteReadySnapshot(snapshot)
        && authoritative.instanceid() == logicalInstanceID
        && authoritative.requestid() == logicalRequestID
        && authoritative.tenantid() == tenantID
        && authoritative.version() == expectedVersion
        && authoritative.instancestatus().code() == static_cast<int32_t>(InstanceState::PAUSED)
        && IsAuthoritativePausedControlIdentity(authoritative)
        && authoritative.has_snapshotinfo()
        && SnapshotIdentityMatches(authoritative.snapshotinfo(), snapshot);
}

bool TrustedResumeIdentity::MatchesCommittedWinner(const resources::InstanceInfo &authoritative) const
{
    return !logicalRequestID.empty() && !tenantID.empty() && expectedVersion > 0
        && IsCommittedResumeWinner(authoritative, logicalInstanceID, targetAttemptID)
        && authoritative.requestid() == logicalRequestID
        && authoritative.tenantid() == tenantID
        && authoritative.version() == expectedVersion + 1
        && (!authoritative.has_snapshotinfo()
            || SnapshotIdentityMatches(authoritative.snapshotinfo(), snapshot));
}

std::string TrustedResumeIdentity::Digest() const
{
    return IdentityDigest(logicalInstanceID, logicalRequestID, tenantID, targetAttemptID, snapshot,
                          expectedVersion, targetAgentID, protocolVersion);
}

bool ValidateBoundaryIdentity(const std::string &logicalInstanceID, const std::string &logicalRequestID,
                              const std::string &tenantID, const std::string &targetAttemptID,
                              const resources::SnapshotInfo &snapshot, int64_t expectedVersion,
                              const std::string &targetAgentID, int64_t protocolVersion,
                              const std::string &marker)
{
    return !logicalInstanceID.empty() && !logicalRequestID.empty() && !tenantID.empty()
        && !targetAttemptID.empty() && expectedVersion > 0 && !targetAgentID.empty()
        && protocolVersion == PROTOCOL_VERSION && IsCompleteReadySnapshot(snapshot)
        && marker == IdentityDigest(logicalInstanceID, logicalRequestID, tenantID, targetAttemptID, snapshot,
                                    expectedVersion, targetAgentID, protocolVersion);
}

}  // namespace functionsystem::resume_identity
