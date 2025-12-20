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

#include "function_agent/common/kv_client.h"

namespace functionsystem::function_agent {
Status KVClient::Init(const function_agent::FunctionAgentFlags &flags)
{
    datasystem::ConnectOptions connectOptions;
    connectOptions.host = flags.GetDataSystemHost();
    connectOptions.port = flags.GetDataSystemPort();
    dsKvClient_ = std::make_unique<datasystem::KVClient>(connectOptions);
    ::datasystem::Status s = dsKvClient_->Init();
    if (s.IsError()) {
        return Status(StatusCode::BP_DATASYSTEM_ERROR, s.ToString());
    }
    return Status::OK();
}
std::pair<Status, datasystem::ReadOnlyBuffer> KVClient::Get(const std::string &key)
{
    datasystem::Optional<datasystem::ReadOnlyBuffer> buffer;
    datasystem::Status s = dsKvClient_->Get(key, buffer);
    if (s.IsError()) {
        return std::make_pair(Status(StatusCode::BP_DATASYSTEM_ERROR, s.ToString()), datasystem::ReadOnlyBuffer());
    }
    return std::make_pair(Status::OK(), *buffer);
}
}  // namespace functionsystem::function_agent
