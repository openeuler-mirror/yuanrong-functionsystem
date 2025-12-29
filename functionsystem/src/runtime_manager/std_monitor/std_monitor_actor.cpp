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

#include "std_monitor_actor.h"

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/exec_utils.h"

namespace functionsystem {
const int MAX_EVENTS_NUM = 1024;
const int DEFAULT_USER_LOG_EPOLL_ACTIVATE_INTERVAL_MS = 1;
const long DEFAULT_USER_LOG_EPOLL_PIPE_MAX_SIZE = 1024 * 1024;

StdMonitorActor::StdMonitorActor(const std::string &name) : ActorBase(name)
{
}

StdMonitorActor::~StdMonitorActor()
{
    Stop();
}

void StdMonitorActor::Finalize()
{
    YRLOG_INFO("finalize StdMonitorActor {}", ActorBase::GetAID().Name());
}

litebus::Future<bool> StdMonitorActor::Start()
{
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ == EPOLL_FD_INVALID) {
        YRLOG_ERROR("Failed to create epoll fd");
        return false;
    }
    UpdatePipeMaxSizeFromSystem();
    if (!AddStopEvent()) {
        YRLOG_ERROR("Failed to add monitoring for stopEvent");
        return false;
    }
    isRunning_ = true;
    thread_ = std::make_unique<std::thread>([this]() { RunLoop(); });
    YRLOG_INFO("Successfully started StdMonitorActor");
    return true;
}

void StdMonitorActor::UpdatePipeMaxSizeFromSystem()
{
    pipeMaxSize_ = DEFAULT_USER_LOG_EPOLL_PIPE_MAX_SIZE;
    std::ifstream fs("/proc/sys/fs/pipe-max-size");
    long systemPipeMaxSize;
    if ((fs) && (fs >> systemPipeMaxSize)) {
        pipeMaxSize_ = std::min(pipeMaxSize_, systemPipeMaxSize);
        YRLOG_INFO("pipe-max-size set to: {}", pipeMaxSize_);
    } else {
        YRLOG_ERROR("failed to get pipe-max-size, using default");
    }
}

void StdMonitorActor::SetPipeMaxSize(int fd)
{
    fcntl(fd, F_SETPIPE_SZ, pipeMaxSize_);
    if (fcntl(fd, F_SETPIPE_SZ, pipeMaxSize_) == -1) {
        YRLOG_WARN("Failed to set pipe max size for fd: {}", fd);
    }
}

bool StdMonitorActor::SetNonBlockingIO(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        YRLOG_ERROR("Failed to set non-blocking for fd: {}", fd);
        return false;
    }
    return true;
}

void StdMonitorActor::UpdateActiveTime()
{
    auto now = std::chrono::steady_clock::now();
    auto it = fdActiveTimestamps_.begin();
    while (it != fdActiveTimestamps_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        if (elapsed >= DEFAULT_USER_LOG_EPOLL_ACTIVATE_INTERVAL_MS) {
            litebus::Async(GetAID(), &StdMonitorActor::HandleEvent, it->first, EPOLLIN);
            it = fdActiveTimestamps_.erase(it);
        } else {
            ++it;
        }
    }
}

bool StdMonitorActor::IsReaderActive(int fd)
{
    return fdActiveTimestamps_.count(fd) > 0;
}

void StdMonitorActor::MarkReaderActive(int fd)
{
    auto now = std::chrono::steady_clock::now();
    fdActiveTimestamps_[fd] = now;
}

void StdMonitorActor::RunLoop()
{
    struct epoll_event events[MAX_EVENTS_NUM];
    while (isRunning_) {
        int nfds = epoll_wait(epollFd_, events, MAX_EVENTS_NUM, -1);
        UpdateActiveTime();
        if (nfds == -1) {
            YRLOG_ERROR("Failed to do epoll_wait, code:{}, err:{}", errno, litebus::os::Strerror(errno));
            continue;
        }
        if (ShouldStopRunLoop()) {
            YRLOG_INFO("receive stop event, start to exit run loop");
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            int ev = events[i].events;
            if (!IsReaderActive(fd)) {
                litebus::Async(GetAID(), &StdMonitorActor::HandleEvent, fd, ev);
                MarkReaderActive(fd);
            }
        }
    }
}

litebus::Future<bool> StdMonitorActor::AddFd(int fd, int events, StdEventHandler callback)
{
    if (epollFd_ == EPOLL_FD_INVALID) {
        YRLOG_WARN("Std monitor is not initialized yet");
        return false;
    }

    if (fd < 0) {
        YRLOG_WARN("Invalid fd: {}", fd);
        return false;
    }

    if (!SetNonBlockingIO(fd)) {
        return false;
    }

    SetPipeMaxSize(fd);

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        YRLOG_ERROR("Failed to add monitoring for fd: {}", fd);
        return false;
    }
    fdCallbacks_[fd] = callback;
    YRLOG_DEBUG("Successful monitoring added for fd: {}", fd);
    return true;
}

bool StdMonitorActor::StopMonitoringFd(int fd)
{
    if (epollFd_ == EPOLL_FD_INVALID) {
        YRLOG_WARN("Std monitor is not initialized yet");
        return false;
    }

    if (fd < 0) {
        YRLOG_WARN("Invalid fd: {}", fd);
        return false;
    }

    if (fdCallbacks_.find(fd) == fdCallbacks_.end()) {
        YRLOG_DEBUG("The monitored fd({}) has been deleted", fd);
        return true;
    }

    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) != 0) {
        YRLOG_ERROR("Failed to remove fd {}: code:{}, err:{}", fd, errno, litebus::os::Strerror(errno));
        return false;
    }
    return true;
}

litebus::Future<bool> StdMonitorActor::RemoveFd(int fd)
{
    if (!StopMonitoringFd(fd)) {
        return false;
    }
    fdCallbacks_.erase(fd);
    YRLOG_DEBUG("Successfully removed monitoring of fd: {}", fd);
    return true;
}

void StdMonitorActor::HandleEvent(int fd, int event)
{
    auto it = fdCallbacks_.find(fd);
    if (it == fdCallbacks_.end()) {
        YRLOG_ERROR("Empty fdCallback, fd: {}", fd);
        return;
    }
    const auto& callback = it->second;
    if (callback) {
        callback(fd, event);
    }
}

bool StdMonitorActor::AddStopEvent()
{
    stopEventfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (stopEventfd_ < 0) {
        YRLOG_ERROR("Failed to create stopEvent fd");
        return false;
    }

    auto event = EPOLLIN | EPOLLHUP | EPOLLERR;
    if (!AddFd(stopEventfd_, event, nullptr).Get()) {
        close(stopEventfd_);
        stopEventfd_ = -1;
        return false;
    }
    return true;
}

bool StdMonitorActor::ShouldStopRunLoop()
{
    uint64_t stopSignal;
    if (read(stopEventfd_, &stopSignal, sizeof(stopSignal)) == static_cast<ssize_t>(sizeof(stopSignal))) {
        isRunning_ = false;
        return true;
    }
    return false;
}

void StdMonitorActor::StopRunLoop()
{
    // write stop event
    uint64_t stopSignal = 1;
    if (write(stopEventfd_, &stopSignal, sizeof(stopSignal)) != static_cast<ssize_t>(sizeof(stopSignal))) {
        YRLOG_WARN("fail to write stopEventfd, fd:{}, code:{}, errno:{}",
                   stopEventfd_, errno, litebus::os::Strerror(errno));
    }
}

void StdMonitorActor::Stop()
{
    if (!isRunning_) {
        YRLOG_INFO("std monitor already stopped");
        return;
    }
    isRunning_ = false;

    StopRunLoop();
    ASSERT_IF_NULL(thread_);
    if (thread_->joinable()) {
        YRLOG_INFO("wait for std monitor thread to exit");
        thread_->join();
    }

    // clean monitor events
    for (auto fdPair : fdCallbacks_) {
        int fd = fdPair.first;
        StopMonitoringFd(fd);
    }
    fdCallbacks_.clear();

    // close epoll
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = EPOLL_FD_INVALID;
    }

    if (stopEventfd_ >= 0) {
        close(stopEventfd_);
        stopEventfd_ = EPOLL_FD_INVALID;
    }
    YRLOG_INFO("std monitor stopped");
}

}
