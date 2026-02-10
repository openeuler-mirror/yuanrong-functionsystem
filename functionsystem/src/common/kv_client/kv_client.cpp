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

#include "common/kv_client/kv_client.h"

namespace functionsystem {

Status KVClient::Init(const std::string &host, int32_t port)
{
    YRLOG_INFO("initializing kv client with host: {}, port: {}", host, port);
    datasystem::ConnectOptions connectOptions;
    connectOptions.host = host;
    connectOptions.port = port;
    dsKvClient_ = std::make_unique<datasystem::KVClient>(connectOptions);
    ::datasystem::Status s = dsKvClient_->Init();
    if (s.IsError()) {
        YRLOG_ERROR("failed to initialize kv client, host: {}, port: {}, error: {}", host, port, s.ToString());
        return Status(StatusCode::BP_DATASYSTEM_ERROR, s.ToString());
    }
    YRLOG_INFO("kv client initialized successfully with host: {}, port: {}", host, port);
    return Status::OK();
}
std::pair<Status, datasystem::ReadOnlyBuffer> KVClient::Get(const std::string &key)
{
    if (dsKvClient_ == nullptr) {
        YRLOG_ERROR("kv client is not initialized");
        return std::make_pair(Status(StatusCode::BP_DATASYSTEM_ERROR, "kv client is not initialized"),
                              datasystem::ReadOnlyBuffer());
    }

    datasystem::Optional<datasystem::ReadOnlyBuffer> buffer;
    datasystem::Status s = dsKvClient_->Get(key, buffer);
    if (s.IsError()) {
        return std::make_pair(Status(StatusCode::BP_DATASYSTEM_ERROR, s.ToString()), datasystem::ReadOnlyBuffer());
    }
    return std::make_pair(Status::OK(), *buffer);
}

Status KVClient::Put(const std::string &key, const std::string &value)
{
    if (dsKvClient_ == nullptr) {
        YRLOG_ERROR("kv client is not initialized");
        return Status(StatusCode::BP_DATASYSTEM_ERROR, "kv client is not initialized");
    }

    datasystem::Status s = dsKvClient_->Set(key, value);
    if (s.IsError()) {
        YRLOG_ERROR("failed to put key: {}, error: {}", key, s.ToString());
        return Status(StatusCode::BP_DATASYSTEM_ERROR, s.ToString());
    }
    YRLOG_DEBUG("successfully put key: {}", key);
    return Status::OK();
}

Status KVClient::Delete(const std::string &key)
{
    if (dsKvClient_ == nullptr) {
        YRLOG_ERROR("kv client is not initialized");
        return Status(StatusCode::BP_DATASYSTEM_ERROR, "kv client is not initialized");
    }

    datasystem::Status s = dsKvClient_->Del(key);
    if (s.IsError()) {
        YRLOG_ERROR("failed to delete key: {}, error: {}", key, s.ToString());
        return Status(StatusCode::BP_DATASYSTEM_ERROR, s.ToString());
    }
    YRLOG_DEBUG("successfully deleted key: {}", key);
    return Status::OK();
}
}  // namespace functionsystem
