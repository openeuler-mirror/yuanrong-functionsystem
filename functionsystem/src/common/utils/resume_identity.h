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
#ifndef COMMON_UTILS_RESUME_IDENTITY_H
#define COMMON_UTILS_RESUME_IDENTITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::resume_identity {

inline constexpr char EXTENSION_PREFIX[] = "yr.internal.resume.";
inline constexpr char MASTER_MARKER_EXTENSION[] = "yr.internal.resume.target_attempt_id";
inline constexpr char AGENT_MARKER_EXTENSION[] = "yr.internal.resume.agent_identity";
inline constexpr char RUNTIME_MANAGER_MARKER_EXTENSION[] = "yr.internal.resume.runtime_manager_identity";
inline constexpr char EXECUTOR_MARKER_EXTENSION[] = "yr.internal.resume.executor_identity";
inline constexpr char LOGICAL_REQUEST_EXTENSION[] = "yr.internal.resume.logical_request_id";
inline constexpr char EXPECTED_VERSION_EXTENSION[] = "yr.internal.resume.expected_version";
inline constexpr char TARGET_AGENT_EXTENSION[] = "yr.internal.resume.target_agent_id";
inline constexpr char PROTOCOL_VERSION_EXTENSION[] = "yr.internal.resume.protocol_version";
inline constexpr int64_t PROTOCOL_VERSION = 1;
inline constexpr char RUNTIME_ID_PREFIX[] = "yr-resume-";

bool IsResultUnknownStatusCode(int32_t code);
bool IsResumeProtocolExtension(const std::string &key);
bool IsReservedExtension(const std::string &key);

template <typename ExtensionMap>
bool HasReservedExtension(const ExtensionMap &extensions)
{
    for (const auto &[key, value] : extensions) {
        (void)value;
        if (IsReservedExtension(key)) {
            return true;
        }
    }
    return false;
}

template <typename ExtensionMap>
void StripReservedExtensions(ExtensionMap *extensions)
{
    std::vector<std::string> keys;
    for (const auto &[key, value] : *extensions) {
        (void)value;
        if (IsReservedExtension(key)) {
            keys.emplace_back(key);
        }
    }
    for (const auto &key : keys) {
        extensions->erase(key);
    }
}

std::string Sha256Hex(const std::string &input);
bool ParsePositiveInt64(const std::string &value, int64_t *result);
bool IsCompleteReadySnapshot(const resources::SnapshotInfo &snapshot);
bool ValidateReusableSnapshotRestore(const ::messages::ReusableSnapshotRestore &restore);
std::string IdentityDigest(const std::string &logicalInstanceID, const std::string &logicalRequestID,
                           const std::string &tenantID, const std::string &targetAttemptID,
                           const resources::SnapshotInfo &snapshot, int64_t expectedVersion,
                           const std::string &targetAgentID, int64_t protocolVersion = PROTOCOL_VERSION);
std::string ExecutorMarker(const std::string &logicalInstanceID, const std::string &logicalRequestID,
                           const std::string &tenantID, const std::string &targetAttemptID,
                           const resources::SnapshotInfo &snapshot, int64_t expectedVersion,
                           const std::string &targetAgentID, int64_t protocolVersion = PROTOCOL_VERSION);
bool ParseAndValidateExecutorMarker(const std::string &logicalInstanceID, const std::string &tenantID,
                                    const std::string &targetAttemptID, const resources::SnapshotInfo &snapshot,
                                    int64_t expectedVersion, const std::string &targetAgentID,
                                    int64_t protocolVersion, const std::string &marker,
                                    std::string *logicalRequestID);
std::string RuntimeID(const std::string &logicalInstanceID, const std::string &targetAttemptID);
bool IsExactResumeTargetCleanupRequest(const std::string &cleanupRequestID,
                                       const std::string &logicalInstanceID, const std::string &runtimeID);
bool ParseExactResumeTargetCleanupRequest(const std::string &cleanupRequestID, const std::string &runtimeID,
                                          std::string *logicalInstanceID, std::string *targetAttemptID);
bool IsCommittedResumeWinner(const resources::InstanceInfo &authoritative, const std::string &logicalInstanceID,
                             const std::string &targetAttemptID);
bool SnapshotIdentityMatches(const resources::SnapshotInfo &left, const resources::SnapshotInfo &right);
bool IsAuthoritativePausedControlIdentity(const resources::InstanceInfo &instance);
Status ValidateMasterSchedule(const messages::ScheduleRequest &request,
                              const resources::InstanceInfo &authoritative);

struct TrustedResumeIdentity {
    std::string logicalInstanceID;
    std::string logicalRequestID;
    std::string tenantID;
    std::string targetAttemptID;
    resources::SnapshotInfo snapshot;
    int64_t expectedVersion{ 0 };
    std::string targetAgentID;
    int64_t protocolVersion{ PROTOCOL_VERSION };

    static TrustedResumeIdentity FromSchedule(const messages::ScheduleRequest &request);
    bool MatchesSchedule(const messages::ScheduleRequest &request) const;
    bool MatchesAuthoritative(const resources::InstanceInfo &authoritative) const;
    bool MatchesCommittedWinner(const resources::InstanceInfo &authoritative) const;
    std::string Digest() const;
};

bool ValidateBoundaryIdentity(const std::string &logicalInstanceID, const std::string &logicalRequestID,
                              const std::string &tenantID, const std::string &targetAttemptID,
                              const resources::SnapshotInfo &snapshot, int64_t expectedVersion,
                              const std::string &targetAgentID, int64_t protocolVersion,
                              const std::string &marker);

}  // namespace functionsystem::resume_identity

#endif  // COMMON_UTILS_RESUME_IDENTITY_H
