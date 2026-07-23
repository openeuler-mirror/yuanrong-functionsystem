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
namespace {
constexpr uint64_t RESTORE_WRITE_TIMEOUT_MS = 1000;
}

void ServiceRegistry::Init(std::shared_ptr<MetaStorageAccessor> accessor, const RegisterInfo &info)
{
    ServiceRegistry::Init(std::move(accessor), info, DEFAULT_TTL);
}

void ServiceRegistry::Init(std::shared_ptr<MetaStorageAccessor> accessor, const RegisterInfo &info, int ttl)
{
    std::lock_guard<std::mutex> lock(mutex_);
    registerInfo_ = info;
    ttl_ = TtlValidate(ttl) ? ttl : DEFAULT_TTL;
    metaStorageAccessor_ = std::move(accessor);
    stopped_ = false;
    YRLOG_INFO("Succeed to init Busproxy ServiceRegistry, TTL: {}, node: {}", ttl_, registerInfo_.meta.node);
}

Status ServiceRegistry::Register()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return RegisterLocked();
}

Status ServiceRegistry::RegisterLocked()
{
    return RegisterLocked(0);
}

Status ServiceRegistry::RegisterLocked(uint64_t timeoutMs)
{
    if (stopped_) {
        return Status(StatusCode::FAILED, "service registry is stopped");
    }
    YRLOG_INFO("Start Busproxy registry, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    RETURN_STATUS_IF_NULL(metaStorageAccessor_, StatusCode::FAILED, "meta store accessor is nullptr");
    auto registerFuture = metaStorageAccessor_->PutWithLease(registerInfo_.key, Dump(registerInfo_.meta), ttl_);
    Status registerStatus;
    if (timeoutMs == 0) {
        registerStatus = registerFuture.Get();
    } else {
        auto result = registerFuture.Get(timeoutMs);
        if (result.IsNone()) {
            YRLOG_ERROR("Timed out registering service, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
            return Status(StatusCode::FAILED, "service registry timed out");
        }
        registerStatus = result.Get();
    }
    if (!registerStatus.IsOk()) {
        YRLOG_ERROR("Failed to register service, key: {}, node: {}. accessor put response:{}", registerInfo_.key,
                    registerInfo_.meta.node, registerStatus.ToString());
        return Status(StatusCode::FAILED, "service registry failed");
    }
    YRLOG_INFO("Succeed to register Busproxy, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    return Status(StatusCode::SUCCESS);
}

Status ServiceRegistry::ReplaceProxyService(const ProxyServiceMeta &proxyService)
{
    std::lock_guard<std::mutex> lock(mutex_);
    RETURN_STATUS_IF_NULL(metaStorageAccessor_, StatusCode::FAILED, "meta store accessor is nullptr");
    if (stopped_) {
        return Status(StatusCode::FAILED, "service registry is stopped");
    }
    const auto previousProxyService = registerInfo_.meta.proxyService;
    registerInfo_.meta.proxyService = proxyService;
    // Revoke before replacing the leased value. PutWithLease's keep-alive timer
    // captures the value supplied by the first put, so an in-place second put can
    // later resurrect stale capability metadata. The short absent interval is
    // intentional and fail-closed for discovery.
    auto revokeStatus = metaStorageAccessor_->Revoke(registerInfo_.key).Get();
    if (!revokeStatus.IsOk()) {
        registerInfo_.meta.proxyService = previousProxyService;
        YRLOG_ERROR("Failed to revoke proxy service metadata before capability update, key: {}, status: {}",
                    registerInfo_.key, revokeStatus.ToString());
        return Status(StatusCode::FAILED, "service registry proxy capability revoke failed");
    }
    auto status = RegisterLocked();
    if (status.IsError()) {
        // Recovery after the DELETE event must remain fail-closed even when
        // publishing a ready endpoint failed.
        registerInfo_.meta.proxyService = {};
    }
    return status;
}

Status ServiceRegistry::Restore()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        return Status::OK();
    }
    RETURN_STATUS_IF_NULL(metaStorageAccessor_, StatusCode::FAILED, "meta store accessor is nullptr");
    const auto expected = Dump(registerInfo_.meta);
    const auto current = metaStorageAccessor_->Get(registerInfo_.key);
    if (current.IsSome() && current.Get() == expected) {
        return Status::OK();
    }
    YRLOG_WARN("Restore Busproxy registry, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    return RegisterLocked(RESTORE_WRITE_TIMEOUT_MS);
}

litebus::Future<Status> ServiceRegistry::Stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        return Status::OK();
    }
    YRLOG_INFO("Stop Busproxy registry, key: {}, node: {}", registerInfo_.key, registerInfo_.meta.node);
    stopped_ = true;
    RETURN_STATUS_IF_NULL(metaStorageAccessor_, StatusCode::FAILED, "meta store accessor is nullptr");
    return metaStorageAccessor_->Revoke(registerInfo_.key);
}
}  // namespace functionsystem
