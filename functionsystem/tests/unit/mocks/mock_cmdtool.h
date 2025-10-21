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

#ifndef UNIT_MOCKS_MOCK_CMD_TOOL_H
#define UNIT_MOCKS_MOCK_CMD_TOOL_H

#include <gmock/gmock.h>

#include "common/utils/cmd_tool.h"

namespace functionsystem::test {
class MockCmdTools : public CmdTool {
public:
    MockCmdTools() {
    }
    MOCK_METHOD(std::vector<std::string>, GetCmdResult, (const std::string &cmd), (override));
    MOCK_METHOD(std::vector<std::string>, GetCmdResultWithError, (const std::string &cmd), (override));
};
}
#endif  // UNIT_MOCKS_MOCK_CMD_TOOL_H
