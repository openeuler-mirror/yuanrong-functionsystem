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

#ifndef FUNCTIONSYSTEM_HEALTH_MONITOR_H
#define FUNCTIONSYSTEM_HEALTH_MONITOR_H

#include "async/future.hpp"

namespace functionsystem {

const int TIMEOUT_ERROR_CODE = -2;

struct HealthMonitorParam {
    uint32_t maxFailedTimes = 5;
    uint32_t checkIntervalMs = 3000;
    std::string k8sInfo = "";
};

class HealthMonitor : public litebus::ActorBase  {
public:
    explicit HealthMonitor(const HealthMonitorParam &healthMonitorParam);
    ~HealthMonitor() override = default;

    void RegisterHandler(std::function<litebus::Future<std::string>()> request);

    void SubscribeK8sHealth(std::function<void(bool)> callback); // 状态转换，触发

    void Start();

    // for test
    [[maybe_unused]] uint32_t GetFailedTime() const
    {
        return curFailedTime_;
    }

    [[maybe_unused]] bool IsHealthy()
    {
        return healthStatus_.load();
    }

    // for test
    [[maybe_unused]] void RunHealthCallback(bool healthy)
    {
        if (healthCallback_) {
            healthCallback_(healthy);
        }
    };

private:
    std::atomic<bool> healthStatus_ = true; // 初始健康状态设为true
    uint32_t curFailedTime_ = 0; // 当前失败次数
    uint32_t maxFailures_; // 最大失败次数
    uint32_t retryInterval_; // 监测间隔时间
    std::string k8sInfo_; // k8s cluster info
    std::function<litebus::Future<std::string>()> healthRequest_; // 监测k8s状态的http请求
    std::function<void(bool)> healthCallback_; // 状态变化回调

    void Check();
};

}  // namespace functionsystem
#endif // FUNCTIONSYSTEM_HEALTH_MONITOR_H
