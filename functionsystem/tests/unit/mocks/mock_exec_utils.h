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

#ifndef UT_MOCKS_MOCK_EXEC_UTILS_H
#define UT_MOCKS_MOCK_EXEC_UTILS_H

#include <gmock/gmock.h>
#include "common/utils/exec_utils.h"

namespace functionsystem::test {

class MockCommandRunner : public CommandRunner {
public:
    MOCK_METHOD(bool, CheckAndRunCommandWrapper, (const std::string& command), ());
    MOCK_METHOD(CommandExecResult, ExecuteCommandWrapper, (const std::string& command), ());
}; // class MockCommandRunner

} // namespace functionsystem::test

#endif // UT_MOCKS_MOCK_EXEC_UTILS_H