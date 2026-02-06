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

#include "function_proxy/common/exec_session/exec_session_actor.h"
#include "function_proxy/common/exec_session/io_event_actor.h"

namespace functionsystem {
namespace test {

class ExecSessionActorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize IOEventActor singleton for tests
        IOEventActor::CreateInstance();
    }

    void TearDown() override {
        // Cleanup IOEventActor singleton
        IOEventActor::DestroyInstance();
    }
};

// Test ExecSessionActor creation
TEST_F(ExecSessionActorTest, CreateSessionActor) {
    std::vector<std::pair<std::string, int>> outputs;  // (data, exitCode)

    ExecSessionActor::CreateParams params;
    params.writer = [&outputs](const std::string& data, int exitCode) {
        outputs.push_back({data, exitCode});
    };

    auto actor = ExecSessionActor::Create(params);

    ASSERT_NE(actor, nullptr);
    EXPECT_FALSE(actor->GetAID().Name().empty());
}

// Test ExecSessionActor ID uniqueness
TEST_F(ExecSessionActorTest, SessionActorIdUnique) {
    ExecSessionActor::CreateParams params;
    params.writer = [](const std::string& data, int exitCode) {};

    auto actor1 = ExecSessionActor::Create(params);
    auto actor2 = ExecSessionActor::Create(params);

    EXPECT_NE(actor1->GetAID().Name(), actor2->GetAID().Name());
}

// Test IOEventActor singleton
TEST_F(ExecSessionActorTest, IOEventActorSingleton) {
    auto aid1 = IOEventActor::GetInstance();
    auto aid2 = IOEventActor::GetInstance();

    EXPECT_EQ(aid1.Name(), aid2.Name());
    EXPECT_FALSE(aid1.Name().empty());
}

}  // namespace test
}  // namespace functionsystem
