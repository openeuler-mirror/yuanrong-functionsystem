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

#include <gtest/gtest.h>

#include "common/status/status.h"
#include "runtime_manager/executor/sandbox/sandbox_executor.h"

namespace functionsystem::runtime_manager {
namespace {

// IsRetryableWaitError classifies gRPC transport errors that mean sandboxd
// is unreachable mid-Wait. The mapping is provided by Status::GrpcCode2StatusCode:
// GRPC_OK is the base, and adding the ::grpc::StatusCode integer yields the
// internal code. So GRPC_OK+1 == GRPC_CANCELLED, GRPC_OK+14 == GRPC_UNAVAILABLE, etc.

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_UnavailableIsRetryable)
{
    EXPECT_TRUE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_UNAVAILABLE, "Socket closed")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_CancelledIsRetryable)
{
    EXPECT_TRUE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_CANCELLED, "client cancelled")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_DeadlineExceededIsRetryable)
{
    EXPECT_TRUE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_DEADLINE_EXCEEDED, "timed out")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_InternalIsRetryable)
{
    EXPECT_TRUE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_INTERNAL, "internal err")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_OkIsNotRetryable)
{
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(Status::OK()));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_NotFoundIsNotRetryable)
{
    // Sandbox not found means the runtime is genuinely gone, not a transient error.
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_NOT_FOUND, "no such sandbox")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_PermissionDeniedIsNotRetryable)
{
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_PERMISSION_DENIED, "denied")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_InvalidArgumentIsNotRetryable)
{
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_INVALID_ARGUMENT, "bad request")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_AlreadyExistsIsNotRetryable)
{
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_ALREADY_EXISTS, "duplicate")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_UnknownIsNotRetryable)
{
    // GRPC_UNKNOWN is ambiguous; we deliberately exclude it to avoid
    // retrying genuine application errors that surface as UNKNOWN.
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_UNKNOWN, "unknown")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_InnerCommunicationIsNotRetryable)
{
    // Non-gRPC internal codes should never be classified as retryable here.
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(StatusCode::ERR_INNER_COMMUNICATION, "Socket closed")));
}

TEST(SandboxExecutorRetryTest, IsRetryableWaitError_ClassificationDrivenByCodeNotMessage)
{
    // Same message, different code → message must not influence the result.
    EXPECT_FALSE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_NOT_FOUND, "Socket closed connection reset Broken pipe")));
    EXPECT_TRUE(SandboxExecutor::IsRetryableWaitError(
        Status(GRPC_UNAVAILABLE, "")));
}

}  // namespace
}  // namespace functionsystem::runtime_manager
