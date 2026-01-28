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

#pragma once

#include "common/status/status.h"
#include "common/utils/singleton.h"
#include "datasystem/datasystem.h"
#include "function_agent/flags/function_agent_flags.h"

namespace functionsystem::function_agent {

class KVClient : public Singleton<KVClient> {
public:
    Status Init(const function_agent::FunctionAgentFlags &flags);
    Status Init(const std::string &host, int32_t port);
    std::pair<Status, datasystem::ReadOnlyBuffer> Get(const std::string &key);

private:
    std::unique_ptr<datasystem::KVClient> dsKvClient_;
};
}  // namespace functionsystem::function_agent
