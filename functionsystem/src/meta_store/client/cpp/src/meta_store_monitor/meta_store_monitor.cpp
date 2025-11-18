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

#include "meta_store_monitor.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/metrics/metrics_adapter.h"
#include "common/status/status.h"
#include "litebus.hpp"
#include "timer/timertools.hpp"

namespace functionsystem {

const uint32_t MAX_MAJOR_ALARM_MINUTES = 5 * 60;  // seconds
const uint32_t MAX_CONNECT_TIME = 60000;          // ms

MetaStoreMonitorActor::MetaStoreMonitorActor(const std::string &address, const MetaStoreMonitorParam &param,
                                             const std::shared_ptr<meta_store::MaintenanceClient> &metaStoreClient)
    : litebus::ActorBase("MetaStoreMonitorActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString()),
      client_(metaStoreClient),
      address_(address),
      param_(param)
{
}

void MetaStoreMonitorActor::Finalize()
{
    litebus::TimerTools::Cancel(timer_);
}

void MetaStoreMonitorActor::StartMonitor()
{
    client_->BindReconnectedCallBack(
        [aid(GetAID())](const std::string &) { litebus::Async(aid, &MetaStoreMonitorActor::CheckMetaStoreStatus); });
    timer_ = litebus::AsyncAfter(param_.checkIntervalMs, GetAID(), &MetaStoreMonitorActor::CheckMetaStoreStatus);
}

void MetaStoreMonitorActor::CheckMetaStoreStatus()
{
    if (isChecking_) {
        return;
    }
    isChecking_ = true;
    ASSERT_IF_NULL(client_);
    client_->HealthCheck()
        .After(param_.timeoutMs,
               [](const litebus::Future<StatusResponse> &response) {
                   YRLOG_ERROR("check MetaStore connection timeout");
                   response.SetFailed(static_cast<int32_t>(StatusCode::GRPC_UNKNOWN));
                   return response;
               })
        .OnComplete(litebus::Defer(GetAID(), &MetaStoreMonitorActor::OnCheckMetaStoreStatus, std::placeholders::_1));
}

void MetaStoreMonitorActor::OnCheckMetaStoreStatus(const litebus::Future<StatusResponse> &response)
{
    if (response.IsOK() && response.Get().status.IsOk()) {
        OnMetaStoreHealthy();
    } else {
        OnMetaStoreUnhealthy(response);
    }

    litebus::TimerTools::Cancel(timer_);
    timer_ = litebus::AsyncAfter(param_.checkIntervalMs, GetAID(), &MetaStoreMonitorActor::CheckMetaStoreStatus);
    isChecking_ = false;
}

void MetaStoreMonitorActor::OnMetaStoreHealthy()
{
    YRLOG_DEBUG("success to check MetaStore health");
    switch (alarmLevel_) {
        case metrics::AlarmLevel::MAJOR:
            metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(metrics::AlarmLevel::MAJOR,
                param_.backend);
            break;

        case metrics::AlarmLevel::CRITICAL:
            metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(metrics::AlarmLevel::MAJOR,
                param_.backend);
            metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyResolved(metrics::AlarmLevel::CRITICAL,
                param_.backend);
            break;

        case metrics::AlarmLevel::OFF:
            break;

        default:
            break;
    }
    alarmLevel_ = metrics::AlarmLevel::OFF;
    ResetUnHealthy();
}

void MetaStoreMonitorActor::OnMetaStoreUnhealthy(const litebus::Future<StatusResponse> &response)
{
    int32_t errCode = 0;
    std::string errMsg;
    if (response.IsError()) {
        errCode = response.GetErrorCode();
        errMsg = "connect timeout";
    } else {
        errCode = static_cast<int32_t>(response.Get().status.StatusCode());
        errMsg = response.Get().status.GetMessage();
    }
    YRLOG_DEBUG("failed to check MetaStore health, errcode: {}, msg: {}", errCode, errMsg);
    IncreaseUnHealthy(Status(StatusCode(errCode), errMsg));

    auto now = std::chrono::steady_clock::now();
    switch (alarmLevel_) {
        case metrics::AlarmLevel::MAJOR:
            if (std::chrono::duration_cast<std::chrono::seconds>(now - firingBeginTime_).count()
                > MAX_MAJOR_ALARM_MINUTES) {
                alarmLevel_ = metrics::AlarmLevel::CRITICAL;
            }
            metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyFiring(
                alarmLevel_, "code: " + std::to_string(errCode) + ", msg: " + errMsg, param_.backend);
            break;
        case metrics::AlarmLevel::CRITICAL:
            metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyFiring(
                alarmLevel_, "code: " + std::to_string(errCode) + ", msg: " + errMsg, param_.backend);
            break;
        case metrics::AlarmLevel::OFF:
            firingBeginTime_ = std::chrono::steady_clock::now();
            alarmLevel_ = metrics::AlarmLevel::MAJOR;
            metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyFiring(
                alarmLevel_, "code: " + std::to_string(errCode) + ", msg: " + errMsg, param_.backend);
            break;
        default:
            break;
    }
}

void MetaStoreMonitorActor::RegisterHealthyObserver(const std::shared_ptr<MetaStoreHealthyObserver> &observer)
{
    RETURN_IF_NULL(observer);
    (void)observers_.emplace_back(observer);
}

bool MetaStoreMonitorActor::ClearObservers()
{
    observers_.clear();
    return true;
}

void MetaStoreMonitorActor::ResetUnHealthy()
{
    // failed status has already been published
    // recover the healthy status
    if (failedTimes_ >= param_.maxTolerateFailedTimes) {
        YRLOG_INFO("health check of meta store client({}) has already been recovered, observer size({})", address_,
                   observers_.size());
        for (auto observer : observers_) {
            if (observer == nullptr) {
                continue;
            }
            observer->OnHealthyStatus(Status::OK());
        }
    }
    failedTimes_ = 0;
}

void MetaStoreMonitorActor::IncreaseUnHealthy(const Status &status)
{
    failedTimes_++;
    if (failedTimes_ < param_.maxTolerateFailedTimes) {
        return;
    }
    // an integer multiple of maxTolerateFailedTimes will repeatedly to publish an error status.
    if (param_.maxTolerateFailedTimes != 0 && failedTimes_ % param_.maxTolerateFailedTimes != 0) {
        return;
    }
    YRLOG_WARN("health check of meta store client({}) has already been failed {} times, notify to trigger fallbreak",
               address_, failedTimes_);
    for (auto observer : observers_) {
        if (observer == nullptr) {
            continue;
        }
        observer->OnHealthyStatus(status);
    }
}

MetaStoreMonitor::MetaStoreMonitor(const std::string &address, const MetaStoreMonitorParam &param,
                                   const std::shared_ptr<meta_store::MaintenanceClient> &metaStoreClient)
    : client_(metaStoreClient)
{
    actor_ = std::make_shared<MetaStoreMonitorActor>(address, param, metaStoreClient);
    litebus::Spawn(actor_);
}

MetaStoreMonitor::~MetaStoreMonitor()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
    actor_ = nullptr;
    client_ = nullptr;
}

Status MetaStoreMonitor::CheckMetaStoreConnected()
{
    ASSERT_IF_NULL(client_);
    auto res = client_->IsConnected().Get(MAX_CONNECT_TIME);
    if (res.IsNone() || !res.Get()) {
        YRLOG_ERROR("failed to connect to MetaStore");
        metrics::MetricsAdapter::GetInstance().StorageBackendUnhealthyFiring(metrics::AlarmLevel::MAJOR,
                                                                   "msg: failed to connect");
        return Status(StatusCode::FAILED, "failed to connect to MetaStore");
    }
    litebus::Async(actor_->GetAID(), &MetaStoreMonitorActor::StartMonitor);
    return Status::OK();
}

void MetaStoreMonitor::RegisterHealthyObserver(const std::shared_ptr<MetaStoreHealthyObserver> &observer)
{
    litebus::Async(actor_->GetAID(), &MetaStoreMonitorActor::RegisterHealthyObserver, observer);
}

litebus::Future<bool> MetaStoreMonitor::ClearObservers()
{
    return litebus::Async(actor_->GetAID(), &MetaStoreMonitorActor::ClearObservers);
}

void MetaStoreMonitor::StartMonitor()
{
    litebus::Async(actor_->GetAID(), &MetaStoreMonitorActor::StartMonitor);
}
}  // namespace functionsystem
