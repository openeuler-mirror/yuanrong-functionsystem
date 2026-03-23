/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_IO_EVENT_ACTOR_H
#define FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_IO_EVENT_ACTOR_H

#include <sys/epoll.h>

#include <actor/actor.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <timer/timertools.hpp>
#include <unordered_map>

namespace functionsystem {

// IOEventActor: Global singleton actor for managing IO events with epoll
// Provides a generic fd monitoring mechanism with callback support
class IOEventActor : public litebus::ActorBase {
public:
    // IO callback type: (data, exitCode) -> void
    // exitCode: -1 for normal data, >=0 for EOF/error with exit code
    using IOCallback = std::function<void(const std::string &data, int exitCode)>;

    // Create singleton instance (should be called once at service startup)
    static void CreateInstance();

    // Destroy singleton instance (should be called at service shutdown)
    static void DestroyInstance();

    // Get global singleton instance AID
    static litebus::AID GetInstance();

    ~IOEventActor() override;

public:
    // Async-callable methods (normal function signatures)

    // Register fd with callback for IO events
    // dataCallback: invoked when data available / EOF / error
    // onUnregister: invoked after epoll remove (for fd close). Empty = no-op (e.g. stderr when stdout owns cleanup).
    void DoRegister(int fd, IOCallback dataCallback, std::function<void()> onUnregister = nullptr);

    // Unregister fd. Uses onUnregister from Register, or onDone if provided (overrides).
    void DoUnregister(int fd, std::function<void()> onDone = nullptr);

protected:
    void Init() override;
    void Finalize() override;

private:
    explicit IOEventActor(const std::string &name);

    // Event loop (scheduled periodically via AsyncAfter)
    void EventLoop();

    // Helper method to read and dispatch data
    void ReadAndDispatch(int fd);

    struct FdInfo {
        IOCallback dataCb;
        std::function<void()> onUnregister;
    };
    int epollFd_{ -1 };
    std::unordered_map<int, FdInfo> fdToInfo_;
    std::atomic<bool> running_{ false };
    litebus::Timer eventLoopTimer_;  // Timer for event loop scheduling

    static std::shared_ptr<IOEventActor> instance_;
    static constexpr int MAX_EVENTS = 64;
    static constexpr int EVENT_LOOP_INTERVAL_MS = 10;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_IO_EVENT_ACTOR_H
