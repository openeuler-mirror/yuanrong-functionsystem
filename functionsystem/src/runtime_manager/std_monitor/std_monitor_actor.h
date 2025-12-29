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

#ifndef FUNCTIONSYSTEM_STD_EVENT_LOOP_H
#define FUNCTIONSYSTEM_STD_EVENT_LOOP_H

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>

#include "actor/actor.hpp"
#include "async/future.hpp"

namespace functionsystem {
const int EPOLL_FD_INVALID = -1;
using StdEventHandler = std::function<void(int, int)>;

class StdMonitorActor : public litebus::ActorBase {
public:
    explicit StdMonitorActor(const std::string &name);
    ~StdMonitorActor() override;
    litebus::Future<bool> Start();
    void Stop();
    litebus::Future<bool> AddFd(int fd, int events, StdEventHandler callback);
    litebus::Future<bool> RemoveFd(int fd);

private:
    void Finalize() override;
    void RunLoop();
    void HandleEvent(int fd, int event);
    bool AddStopEvent();
    void StopRunLoop();
    bool ShouldStopRunLoop();
    bool StopMonitoringFd(int fd);
    void UpdateActiveTime();
    bool IsReaderActive(int fd);
    void MarkReaderActive(int fd);
    void UpdatePipeMaxSizeFromSystem();
    void SetPipeMaxSize(int fd);
    bool SetNonBlockingIO(int fd);

    std::map<int, StdEventHandler> fdCallbacks_;
    std::unordered_map<int, std::chrono::time_point<std::chrono::steady_clock>> fdActiveTimestamps_;
    std::atomic<bool> isRunning_ { false };
    std::unique_ptr<std::thread> thread_;
    int epollFd_ { EPOLL_FD_INVALID };
    int stopEventfd_ { EPOLL_FD_INVALID };
    long pipeMaxSize_;
};

}
#endif // FUNCTIONSYSTEM_STD_EVENT_LOOP_H
