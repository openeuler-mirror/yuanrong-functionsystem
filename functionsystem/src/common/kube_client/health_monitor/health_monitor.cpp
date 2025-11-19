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
#include "health_monitor.h"

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"

namespace functionsystem {

HealthMonitor::HealthMonitor(const HealthMonitorParam &healthMonitorParam) : litebus::ActorBase("HealthMonitor")
{
    maxFailures_ = healthMonitorParam.maxFailedTimes;
    retryInterval_ = healthMonitorParam.checkIntervalMs;
    k8sInfo_ = healthMonitorParam.k8sInfo;
}

void HealthMonitor::RegisterHandler(std::function<litebus::Future<std::string>()> request)
{
    healthRequest_ = std::move(request);
}

void HealthMonitor::SubscribeK8sHealth(std::function<void(bool)> callback)
{
    healthCallback_ = std::move(callback);
}

void HealthMonitor::Start()
{
    if (!healthStatus_.load()) {
        // 状态不健康，说明已经存在健康监测了，无需再次监测
        YRLOG_WARN("health monitor has already started, status is {}", healthStatus_.load());
        return;
    }
    // 执行开始时，状态健康 healthStatus_ is true：
    // 1. 失败次数+1
    // 2. 如果失败次数未达到阈值，返回
    // 3. 失败次数达到阈值，开始健康检测
    curFailedTime_++;
    YRLOG_INFO("health monitor failed: status is {}, time is {}, maxFailures is {}", healthStatus_.load(),
               curFailedTime_, maxFailures_);
    if (curFailedTime_ <= maxFailures_) {
        return;
    }
    healthStatus_.store(false);

    metrics::MetricsAdapter::GetInstance().SendK8sAlarm(k8sInfo_);

    if (healthCallback_) {
        healthCallback_(false); // healthy -> unhealthy
    }
    YRLOG_INFO("health monitor first check, status is {}", healthStatus_.load());
    litebus::AsyncAfter(retryInterval_, GetAID(), &HealthMonitor::Check);
}

void HealthMonitor::Check()
{
    if (healthRequest_ == nullptr) {
        YRLOG_ERROR("health request is null");
        return;
    }
    YRLOG_INFO("health monitor starts to check, maxFailures is {}, interval is {}ms", maxFailures_, retryInterval_);
    (void)healthRequest_().After(retryInterval_, [this](const litebus::Future<std::string> &future) {
        YRLOG_WARN("health monitor check timeout, status is {}, code is {}", healthStatus_.load(),
                   future.GetErrorCode());
        auto promise = litebus::Promise<std::string>();
        promise.SetFailed(TIMEOUT_ERROR_CODE);
        return promise.GetFuture();
    }).OnComplete([=](const litebus::Future<std::string> &future) {
        YRLOG_WARN("health monitor check OnComplete, status is {}, code is {}", healthStatus_.load(),
                   future.GetErrorCode());
        // response success or response code < 500
        if (future.IsError() || future.GetErrorCode() == TIMEOUT_ERROR_CODE ||
            future.GetErrorCode() >= litebus::http::ResponseCode::INTERNAL_SERVER_ERROR ||
            future.GetErrorCode() == litebus::http::HttpErrorCode::CONNECTION_REFUSED ||
            future.GetErrorCode() == litebus::http::HttpErrorCode::CONNECTION_TIMEOUT) {
            metrics::MetricsAdapter::GetInstance().SendK8sAlarm(k8sInfo_);
            YRLOG_WARN("health monitor check failed, status is {}, retry check", healthStatus_.load());
            litebus::AsyncAfter(future.GetErrorCode() == TIMEOUT_ERROR_CODE ? 0 : retryInterval_,
                                GetAID(), &HealthMonitor::Check);
            return;
        }
        healthStatus_.store(true);
        curFailedTime_ = 0;
        if (healthCallback_) {
            healthCallback_(true); // unhealthy -> healthy
        }
        YRLOG_WARN("health monitor check ok, status is {}", healthStatus_.load());
    });
}

} // namespace functionsystem

