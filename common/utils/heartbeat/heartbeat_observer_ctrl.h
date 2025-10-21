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

#ifndef COMMON_HEARTBEAT_OBSERVER_CTRL_H
#define COMMON_HEARTBEAT_OBSERVER_CTRL_H

#include <async/future.hpp>
#include <memory>
#include <unordered_map>

#include "heartbeat/heartbeat_observer.h"
#include "logs/logging.h"
#include "status/status.h"

namespace functionsystem {
class HeartbeatObserverCtrl {
public:
    HeartbeatObserverCtrl() : HeartbeatObserverCtrl(0, 0){};
    HeartbeatObserverCtrl(uint32_t pingTimes, uint32_t pingCycleMs);
    virtual ~HeartbeatObserverCtrl() = default;
    virtual litebus::Future<Status> Add(const std::string &id, const std::string &address,
                                        const HeartbeatObserver::TimeOutHandler handler);
    virtual void Delete(const std::string &id);

private:
    std::unordered_map<std::string, std::unique_ptr<HeartbeatObserveDriver>> heartbeats_;  // key: ID, etc.
    uint32_t pingTimes_;
    uint32_t pingCycleMs_;
};

}  // namespace functionsystem

#endif  // COMMON_HEARTBEAT_OBSERVER_CTRL_H
