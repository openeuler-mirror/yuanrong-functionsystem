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

#ifndef COMMON_FILE_MONITOR_H
#define COMMON_FILE_MONITOR_H

#include <async/future.hpp>
#include <litebus.hpp>

#include "common/file_monitor/file_monitor_actor.h"

namespace functionsystem {
class FileMonitor {
public:
    FileMonitor(){};
    ~FileMonitor() = default;
    litebus::Future<bool> Start();
    void Stop();
    litebus::Future<bool> AddWatch(const std::string &path, const functionsystem::EventHandler handler);
    litebus::Future<bool> RemoveWatch(const std::string &path);
private:
    std::shared_ptr<FileMonitorActor> actor_{ nullptr };
};
}  // namespace functionsystem

#endif  // COMMON_FILE_MONITOR_H