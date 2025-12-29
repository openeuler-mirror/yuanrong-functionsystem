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

#ifndef FUNCTIONSYSTEM_STD_MONITOR_H
#define FUNCTIONSYSTEM_STD_MONITOR_H

#include <async/future.hpp>
#include <litebus.hpp>

#include "std_monitor_actor.h"

namespace functionsystem {
class StdMonitor {
public:
    StdMonitor(){};
    ~StdMonitor() = default;
    litebus::Future<bool> Start();
    void Stop();
    litebus::Future<bool> AddFd(int fd, int events, StdEventHandler callback);
    litebus::Future<bool> RemoveFd(int fd);
private:
    std::shared_ptr<StdMonitorActor> actor_{ nullptr };
};
}  // namespace functionsystem

#endif // FUNCTIONSYSTEM_STD_MONITOR_H
