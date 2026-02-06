/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include "function_proxy/common/exec_session/exec_session.h"

namespace functionsystem {
namespace test {

class ExecSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test setup if needed
    }

    void TearDown() override {
    }
};

// Test session creation
TEST_F(ExecSessionTest, CreateSession) {
    ExecSession::CreateParams params;
    params.containerId = "test-container";
    params.command = {"/bin/sh"};
    params.tty = false;

    auto session = ExecSession::Create(params);

    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(session->GetSessionId().empty());
    EXPECT_FALSE(session->IsRunning());
}

// Test session ID uniqueness
TEST_F(ExecSessionTest, SessionIdUnique) {
    ExecSession::CreateParams params;
    params.containerId = "test-container";

    auto session1 = ExecSession::Create(params);
    auto session2 = ExecSession::Create(params);

    EXPECT_NE(session1->GetSessionId(), session2->GetSessionId());
}

// Test default command
TEST_F(ExecSessionTest, DefaultCommand) {
    ExecSession::CreateParams params;
    params.containerId = "test-container";
    // Don't set command

    auto session = ExecSession::Create(params);
    ASSERT_NE(session, nullptr);
    // Default should use /bin/sh
}

// Test write to closed session
TEST_F(ExecSessionTest, WriteToClosedSession) {
    ExecSession::CreateParams params;
    params.containerId = "test-container";

    auto session = ExecSession::Create(params);

    // Close without starting
    session->Close();

    auto status = session->WriteInput("test");
    EXPECT_FALSE(status.IsOk());
}

// Test resize in non-TTY mode
TEST_F(ExecSessionTest, ResizeNonTtyMode) {
    ExecSession::CreateParams params;
    params.containerId = "test-container";
    params.tty = false;

    auto session = ExecSession::Create(params);

    auto status = session->Resize(40, 120);
    EXPECT_FALSE(status.IsOk());
}

}  // namespace test
}  // namespace functionsystem
