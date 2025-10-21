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

#include "function_master/global_scheduler/global_sched.h"

#include <gtest/gtest.h>

namespace functionsystem::test {

using namespace functionsystem::global_scheduler;

class GlobalSchedUtilsTest : public ::testing::Test {
};

TEST_F(GlobalSchedUtilsTest, EncodeDecodeExternalAgentID)
{
    std::string a, b, c;
    std::string bb = "123";
    std::string cc = "456";
    EncodeExternalAgentID(a, bb, cc);
    DecodeExternalAgentID(a, b, c);
    EXPECT_EQ(b, bb);
    EXPECT_EQ(c, cc);
}

void AddAgent(std::string &externAgentID, const std::string &localID, const std::string &agentID,
              const std::string &alias, messages::QueryAgentInfoResponse &resp)
{
    resources::AgentInfo agent;
    EncodeExternalAgentID(externAgentID, localID, agentID);
    agent.set_localid(localID);
    agent.set_agentid(agentID);
    agent.set_alias(alias);
    resp.mutable_agentinfos()->Add(std::move(agent));
}

TEST_F(GlobalSchedUtilsTest, ConvertQueryAgentInfoResponseToExternal)
{
    messages::QueryAgentInfoResponse resp;
    messages::ExternalQueryAgentInfoResponse externResp, expected;
    
    std::string externID1, externID2;
    AddAgent(externID1, "local-sched-1", "agent-1", "alias-1", resp);
    AddAgent(externID2, "local-sched-2", "agent-2", "alias-2", resp);

    ConvertQueryAgentInfoResponseToExternal(resp, externResp);
    auto data = externResp.data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_EQ(data[0].id(), externID1);
    EXPECT_EQ(data[1].id(), externID2);
}
}  // namespace functionsystem::test
