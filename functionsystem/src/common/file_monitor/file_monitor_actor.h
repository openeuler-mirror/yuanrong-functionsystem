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

#ifndef COMMON_FILE_MONITOR_ACTOR_H
#define COMMON_FILE_MONITOR_ACTOR_H

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <async/asyncafter.hpp>
#include <atomic>
#include <future>
#include <map>
#include <string>
#include <thread>

#include "common/utils/exec_utils.h"

namespace functionsystem {

using EventHandler = std::function<void(const std::string &, const std::string &, uint32_t)>;

struct EventData {
    int fd;
    int wd;
    std::string path;
    EventHandler handler;
};

class FileMonitorActor : public litebus::ActorBase {
public:
    explicit FileMonitorActor(const std::string &name);
    ~FileMonitorActor() override;

    litebus::Future<bool> Start();
    void Stop();
    litebus::Future<bool> AddWatch(const std::string &path, EventHandler handler);
    litebus::Future<bool> RemoveWatch(const std::string &path);

protected:
    void Init() override;
    void Finalize() override;

private:
    void StartWatchThread();
    bool AddStopEventWatch();
    void CallHandler(EventData *data, std::string name, uint32_t mask);
    int epollFd_ { -1 };
    int stopEventfd_ { -1 };
    std::unordered_map<std::string, std::shared_ptr<EventData>> events_;
    std::unique_ptr<std::thread> watchThread_;
    std::atomic<bool> running_ { false };
};
}  // namespace functionsystem
#endif  // COMMON_FILE_MONITOR_H