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

#include "common/file_monitor/file_monitor_actor.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "async/async.hpp"
#include "common/utils/files.h"

namespace functionsystem {
namespace {
const int64_t EVENT_SIZE = sizeof(struct inotify_event);
const int MAX_EVENTS_NUM = 20;
const int64_t BUF_LEN = MAX_EVENTS_NUM * (EVENT_SIZE + NAME_MAX);
};

FileMonitorActor::FileMonitorActor(const std::string &name) : ActorBase(name)
{
}

FileMonitorActor::~FileMonitorActor()
{
    Stop();
}

void FileMonitorActor::Init()
{
    YRLOG_INFO("init FileMonitorActor {}", ActorBase::GetAID().Name());
}

void FileMonitorActor::Finalize()
{
    YRLOG_INFO("finalize FileMonitorActor {}", ActorBase::GetAID().Name());
}

litebus::Future<bool> FileMonitorActor::Start()
{
    epollFd_ = epoll_create(1);
    if (epollFd_ < 0) {
        YRLOG_ERROR("Failed to create epoll fd");
        return false;
    }

    if (!AddStopEventWatch()) {
        return false;
    }

    running_ = true;
    watchThread_ = std::make_unique<std::thread>([this]() { StartWatchThread(); });
    return true;
}

void FileMonitorActor::StartWatchThread()
{
    while (running_) {
        struct epoll_event events[MAX_EVENTS_NUM];
        int n = epoll_wait(epollFd_, events, MAX_EVENTS_NUM, -1);
        if (n < 0) {
            YRLOG_ERROR("Failed to do epoll_wait, code:{}, err:{}", errno, litebus::os::Strerror(errno));
            continue;
        }

        uint64_t one;
        if (read(stopEventfd_, &one, sizeof(one)) == static_cast<ssize_t>(sizeof(one))) {
            YRLOG_INFO("receive stop event, start to exit watch thread");
            running_ = false;
            break;
        }

        for (int i = 0; i < n; ++i) {
            EventData *data = static_cast<EventData *>(events[i].data.ptr);
            if (data == nullptr) {
                continue;
            }
            int fd = data->fd;
            char buffer[BUF_LEN];
            long length = read(fd, buffer, BUF_LEN);
            if (length < 0) {
                YRLOG_ERROR("Failed to read inotify event, path:{}, code:{}, err:{}",
                            data->path,
                            errno, litebus::os::Strerror(errno));
                continue;
            }
            for (char *p = buffer; p < buffer + length;) {
                struct inotify_event *event = reinterpret_cast<struct inotify_event *>(p);
                CallHandler(data, event->name, event->mask);
                p += sizeof(struct inotify_event) + event->len;
            }
        }
    }
}

void FileMonitorActor::CallHandler(EventData *data, std::string name, uint32_t mask)
{
    if ((mask & IN_CREATE) || (mask & IN_DELETE) || (mask & IN_MODIFY) ||
        (mask & IN_MOVED_TO) || (mask & IN_MOVED_FROM) || (mask & IN_DELETE_SELF)) {
        data->handler(data->path, name, mask);
    }
}

void FileMonitorActor::Stop()
{
    if (!running_) {
        YRLOG_INFO("file monitor already stopped");
        return;
    }
    running_ = false;

    // write stop event
    uint64_t one = 1;
    if (write(stopEventfd_, &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one))) {
        YRLOG_WARN("fail to write stopEventfd, fd:{},errno:{}", stopEventfd_, errno);
    }

    ASSERT_IF_NULL(watchThread_);
    if (watchThread_->joinable()) {
        YRLOG_INFO("wait for file monitor thread to exit");
        watchThread_->join();
    }

    // clean watch events
    for (auto dataPair : events_) {
        int fd = dataPair.second->fd;
        int wd = dataPair.second->wd;
        epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        inotify_rm_watch(fd, wd);
        close(fd);
    }
    events_.clear();

    // close epoll
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }

    if (stopEventfd_ >= 0) {
        close(stopEventfd_);
        stopEventfd_ = -1;
    }
    YRLOG_INFO("file monitor stopped");
}

bool FileMonitorActor::AddStopEventWatch()
{
    stopEventfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (stopEventfd_ < 0) {
        YRLOG_ERROR("Failed to create stopEvent fd");
        return false;
    }

    auto data = std::make_shared<EventData>();
    data->fd = stopEventfd_;
    data->wd = -1;
    data->path = "";
    data->handler = nullptr;

    events_[""] = data;

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.ptr = data.get();

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, stopEventfd_, &ev) < 0) {
        YRLOG_ERROR("Failed to add stopEvent fd to epoll");
        close(stopEventfd_);
        events_.erase("");
        return false;
    }
    return true;
}

litebus::Future<bool> FileMonitorActor::RemoveWatch(const std::string &path)
{
    if (path.empty() || events_.find(path) == events_.end()) {
        YRLOG_DEBUG("watch of path: {} already deleted", path);
        return false;
    }
    auto data = events_[path];
    int fd = data->fd;
    int wd = data->wd;
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    inotify_rm_watch(fd, wd);
    close(fd);
    events_.erase(path);
    YRLOG_DEBUG("succeed to delete watch of path : {}", path);
    return true;
}

litebus::Future<bool> FileMonitorActor::AddWatch(const std::string &path, EventHandler handler)
{
    if (!litebus::os::ExistPath(path)) {
        YRLOG_DEBUG("path {} doesn't exist now, no need to add watch", path);
        return false;
    }
    int fd = inotify_init();
    if (fd < 0) {
        YRLOG_ERROR("Failed to create inotify fd for path: {}", path);
        return false;
    }

    int wd = inotify_add_watch(fd, path.c_str(), IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO |
                                                 IN_MOVED_FROM | IN_DELETE_SELF);
    if (wd < 0) {
        const char* error_message = strerror(errno);
        YRLOG_ERROR("Failed to add watch for path: {}, reason: {}", path, error_message);
        close(fd);
        return false;
    }

    auto data = std::make_shared<EventData>();
    data->fd = fd;
    data->wd = wd;
    data->path = path;
    data->handler = handler;
    events_[path] = data;

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = data.get();
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        YRLOG_ERROR("Failed to add fd to epoll for path: {}", path);
        inotify_rm_watch(fd, wd);
        close(fd);
        events_.erase(path);
        return false;
    }

    YRLOG_INFO("Succeed to add watch for path: {}", path);
    return true;
}
}  // namespace functionsystem