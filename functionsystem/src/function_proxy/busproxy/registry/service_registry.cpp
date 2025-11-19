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

#include "service_registry.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#include "common/logs/logging.h"

namespace functionsystem {

void ServiceRegistry::Init(std::shared_ptr<MetaStorageAccessor> accessor, const RegisterInfo &info)
{
    ServiceRegistry::Init(std::move(accessor), info, DEFAULT_TTL);
}

void ServiceRegistry::Init(std::shared_ptr<MetaStorageAccessor> accessor, const RegisterInfo &info, int ttl)
{
    registerInfo_ = info;
    ttl_ = TtlValidate(ttl) ? ttl : DEFAULT_TTL;
    metaStorageAccessor_ = std::move(accessor);
    YRLOG_INFO("Succeed to init Busproxy ServiceRegistry, TTL: {}, node: {}", ttl_, registerInfo_.meta.node);
}

Status ServiceRegistry::Register()
{
    YRLOG_INFO("Start Busproxy registry, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    RETURN_STATUS_IF_NULL(metaStorageAccessor_, StatusCode::FAILED, "meta store accessor is nullptr");
    Status registerStatus = metaStorageAccessor_->PutWithLease(registerInfo_.key, Dump(registerInfo_.meta), ttl_).Get();
    if (!registerStatus.IsOk()) {
        YRLOG_ERROR("Failed to register service, key: {}, node: {}. accessor put response:{}", registerInfo_.key,
                    registerInfo_.meta.node, registerStatus.ToString());
        return Status(StatusCode::FAILED, "service registry failed");
    }
    YRLOG_INFO("Succeed to register Busproxy, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    return Status(StatusCode::SUCCESS);
}

litebus::Future<Status> ServiceRegistry::Stop()
{
    YRLOG_INFO("Stop Busproxy registry, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    return metaStorageAccessor_->Revoke(registerInfo_.key);
}
}  // namespace functionsystem
