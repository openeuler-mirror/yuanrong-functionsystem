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

#include "io_event_actor.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "actor/aid.hpp"
#include "async/asyncafter.hpp"
#include "common/logs/logging.h"

namespace functionsystem {

// Static member initialization
std::shared_ptr<IOEventActor> IOEventActor::instance_ = nullptr;

void IOEventActor::CreateInstance()
{
    if (instance_ != nullptr) {
        YRLOG_WARN("IOEventActor instance already exists");
        return;
    }

    instance_ = std::shared_ptr<IOEventActor>(new IOEventActor("IOEventActor"));
    litebus::Spawn(instance_);
    YRLOG_INFO("IOEventActor singleton created");
}

litebus::AID IOEventActor::GetInstance()
{
    if (instance_ == nullptr) {
        YRLOG_ERROR("IOEventActor instance not created. Call CreateInstance() first.");
        return litebus::AID();
    }

    return instance_->GetAID();
}

void IOEventActor::DestroyInstance()
{
    if (instance_ != nullptr) {
        YRLOG_INFO("Destroying IOEventActor singleton");
        instance_.reset();
    }
}

IOEventActor::IOEventActor(const std::string &name) : litebus::ActorBase(name)
{
}

IOEventActor::~IOEventActor()
{
}

void IOEventActor::Init()
{
    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        YRLOG_ERROR("Failed to create epoll instance, errno: {}", errno);
        return;
    }

    running_ = true;
    eventLoopTimer_ = litebus::AsyncAfter(EVENT_LOOP_INTERVAL_MS, GetAID(), &IOEventActor::EventLoop);
}

void IOEventActor::Finalize()
{
    running_ = false;

    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }

    fdToInfo_.clear();
}

void IOEventActor::DoRegister(int fd, IOCallback dataCallback, std::function<void()> onUnregister)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        YRLOG_ERROR("Failed to add fd {} to epoll, errno: {}", fd, errno);
        return;
    }
    fdToInfo_[fd] = {std::move(dataCallback), std::move(onUnregister)};
}

void IOEventActor::DoUnregister(int fd, std::function<void()> onDone)
{
    int err = 0;
    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        err = errno;
        // EBADF: fd already closed (e.g. by EOF path's DoCleanupAfterUnregister). ENOENT: fd not in epoll.
        if (err != EBADF && err != ENOENT) {
            YRLOG_ERROR("Failed to remove fd {} from epoll, errno: {}", fd, err);
        }
    }
    std::function<void()> toCall;
    auto it = fdToInfo_.find(fd);
    if (onDone) {
        toCall = std::move(onDone);
    } else if (it != fdToInfo_.end() && it->second.onUnregister) {
        toCall = std::move(it->second.onUnregister);
    }
    fdToInfo_.erase(fd);
    if (toCall) {
        toCall();
    }
}

void IOEventActor::EventLoop()
{
    if (!running_ || epollFd_ < 0) {
        return;
    }

    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollFd_, events, MAX_EVENTS, 0);

    if (nfds < 0) {
        if (errno == EINTR) {
            eventLoopTimer_ = litebus::AsyncAfter(EVENT_LOOP_INTERVAL_MS, GetAID(), &IOEventActor::EventLoop);
            return;
        }
        YRLOG_ERROR("epoll_wait failed, errno: {}", errno);
        return;
    }

    for (int i = 0; i < nfds; i++) {
        int fd = events[i].data.fd;

        if (events[i].events & EPOLLIN) {
            ReadAndDispatch(fd);
        } else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
            auto it = fdToInfo_.find(fd);
            if (it != fdToInfo_.end()) {
                it->second.dataCb("", 0);
            }
            DoUnregister(fd);
        }
    }

    if (running_) {
        eventLoopTimer_ = litebus::AsyncAfter(EVENT_LOOP_INTERVAL_MS, GetAID(), &IOEventActor::EventLoop);
    }
}

void IOEventActor::ReadAndDispatch(int fd)
{
    char buffer[4096];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

    auto it = fdToInfo_.find(fd);
    if (it == fdToInfo_.end()) {
        return;
    }

    if (bytesRead > 0) {
        it->second.dataCb(std::string(buffer, bytesRead), -1);
    } else if (bytesRead == 0) {
        it->second.dataCb("", 0);
        DoUnregister(fd);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        YRLOG_ERROR("read error on fd {}, errno: {}", fd, errno);
        it->second.dataCb("", 0);
        DoUnregister(fd);
    }
}

}  // namespace functionsystem
