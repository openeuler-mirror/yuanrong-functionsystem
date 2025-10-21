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

#include <atomic>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <securec.h>
#include <csignal>
#include <unistd.h>

#include "actor/buslog.hpp"
#include "evloop/evloop.hpp"

namespace litebus {

int EventLoopRun(EvLoop *evloop, int timeout)
{
    int nevent = 0;
    struct epoll_event *events = nullptr;

    (void)sem_post(&evloop->semId);

    size_t size = sizeof(struct epoll_event) * EPOLL_EVENTS_SIZE;
    events = (struct epoll_event *)malloc(size);
    if (events == nullptr) {
        BUSLOG_ERROR("malloc events fail");
        return BUS_ERROR;
    }
    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(events, size, 0, size);

    while (!evloop->stopLoop) {
        /* free deleted event handlers */
        evloop->EventFreeDelEvents();

        BUSLOG_DEBUG("timeout:{},epoll_fd:{}", timeout, evloop->efd);
        nevent = epoll_wait(evloop->efd, events, EPOLL_EVENTS_SIZE, timeout);
        BUSLOG_DEBUG("nevent:{},epoll_fd:{}", nevent, evloop->efd);
        if (nevent < 0) {
            if (errno != EINTR) {
                BUSLOG_ERROR("epoll_wait failed, epoll_fd:{},errno:{}", evloop->efd, errno);
                free(events);
                return BUS_ERROR;
            } else {
                continue;
            }
        } else if (nevent > 0) {
            /* save the epoll modify in "stop" while dispatching handlers */
            evloop->HandleEvent(events, nevent);
        } else {
            BUSLOG_ERROR("epoll_wait failed, epoll_fd:{},ret:0,errno:{}", evloop->efd, errno);
            evloop->stopLoop = 1;
        }

        if (evloop->stopLoop) {
            /* free deleted event handlers */
            evloop->EventFreeDelEvents();
        }
    }
    evloop->stopLoop = 0;
    BUSLOG_INFO("event epoll loop run end");
    free(events);
    return BUS_OK;
}

void *EvloopRun(void *arg)
{
    if (arg == nullptr) {
        BUSLOG_ERROR("arg is null");
    } else {
        (void)EventLoopRun((EvLoop *)arg, -1);
    }
    return nullptr;
}

void QueueReadyCallback(int fd, uint32_t events, void *arg)
{
    EvLoop *evloop = (EvLoop *)arg;
    if (evloop == nullptr) {
        BUSLOG_ERROR("evloop is null, fd:{},events:{}", fd, events);
        return;
    }

    uint64_t count;
    if (read(evloop->queueEventfd, &count, sizeof(count)) == static_cast<ssize_t>(sizeof(count))) {
        // take out functions from the queue
        std::queue<std::function<void()>> q;

        evloop->queueMutex.lock();
        evloop->queue.swap(q);
        evloop->queueMutex.unlock();

        // invoke functions in the queue
        while (!q.empty()) {
            q.front()();
            q.pop();
        }
    }
}

void EvLoop::CleanUp()
{
    if (queueEventfd != -1) {
        (void)close(queueEventfd);
        queueEventfd = -1;
    }

    if (efd != -1) {
        (void)close(efd);
        efd = -1;
    }
}

int EvLoop::AddFuncToEvLoop(std::function<void()> &&func)
{
    // put func to the queue
    queueMutex.lock();
    queue.emplace(std::move(func));
    // return the queue size to send's caller.
    int result = queue.size() > INT32_MAX ? INT32_MAX : static_cast<int>(queue.size());
    queueMutex.unlock();

    if (result == 1) {
        // wakeup event loop
        uint64_t one = 1;
        if (write(queueEventfd, &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one))) {
            BUSLOG_WARN("fail to write queueEventfd, fd:{},errno:{}", queueEventfd, errno);
        }
    }

    return result;
}

bool EvLoop::Init(const std::string &threadName)
{
    int retval = EventLoopCreate();
    if (retval != BUS_OK) {
        return false;
    }
    (void)sem_init(&semId, 0, 0);

    if (pthread_create(&loopThread, nullptr, EvloopRun, static_cast<void *>(this)) != 0) {
        BUSLOG_ERROR("pthread_create fail");
        Finish();
        return false;
    }
    // wait EvloopRun
    (void)sem_wait(&semId);
#if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 12
    std::string name = threadName;
    if (name.empty()) {
        name = "EventLoopThread";
    }
    retval = pthread_setname_np(loopThread, name.c_str());
    if (retval != 0) {
        BUSLOG_INFO("set pthread name fail, name:{},retval:{}", name, retval);
    } else {
        BUSLOG_INFO("set pthread name success, name:{},loopThread:{}", name, loopThread);
    }
#endif

    return true;
}

void EvLoop::Finish()
{
    if (loopThread) {
        void *threadResult = nullptr;
        StopEventLoop();
        int ret = pthread_join(loopThread, &threadResult);
        if (ret != 0) {
            BUSLOG_INFO("pthread_join loopThread fail");
        }
        loopThread = 0;
    }
    EventLoopDestroy();

    BUSLOG_INFO("stop loop succ");
}

EvLoop::~EvLoop()
{
    try {
        Finish();
    } catch (...) {
        // Ignore
    }
}

void EvLoop::DeleteEvent(int fd)
{
    auto iter = events.find(fd);
    if (iter == events.end()) {
        BUSLOG_DEBUG("not found event, fd:{}", fd);
        return;
    }
    BUSLOG_DEBUG("erase event, fd:{}", fd);
    EventData *eventData = iter->second;
    if (eventData != nullptr) {
        delete eventData;
    }
    (void)events.erase(fd);
}

EventData *EvLoop::FindEvent(int fd)
{
    auto iter = events.find(fd);
    if (iter == events.end()) {
        return nullptr;
    }

    return iter->second;
}
void EvLoop::AddEvent(EventData *eventData)
{
    if (!eventData) {
        return;
    }
    DeleteEvent(eventData->fd);
    (void)events.emplace(eventData->fd, eventData);
}

int EvLoop::EventLoopCreate(void)
{
    int retval;

    stopLoop = 0;
    efd = epoll_create(EPOLL_SIZE);
    if (efd == -1) {
        BUSLOG_ERROR("epoll_create fail, errno:{}", errno);
        CleanUp();
        return BUS_ERROR;
    }

    // create eventfd
    queueEventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (queueEventfd == -1) {
        BUSLOG_ERROR("eventfd fail, errno:{}", errno);
        CleanUp();
        return BUS_ERROR;
    }

    retval =
        AddFdEvent(queueEventfd,
                   static_cast<uint32_t>(EPOLLIN) | static_cast<uint32_t>(EPOLLHUP) | static_cast<uint32_t>(EPOLLERR),
                   QueueReadyCallback, (void *)this);
    if (retval != BUS_OK) {
        BUSLOG_ERROR("add queue event fail, queueEventfd:{}", queueEventfd);
        CleanUp();
        return BUS_ERROR;
    }

    return BUS_OK;
}

int EvLoop::AddFdEvent(int fd, uint32_t tEvents, EventHandler handler, void *data)
{
    struct epoll_event ev;
    EventData *evdata = nullptr;
    int ret;

    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(&ev, sizeof(ev), 0, sizeof(ev));

    ev.events = tEvents;

    evdata = new (std::nothrow) EventData();
    if (evdata == nullptr) {
        BUSLOG_ERROR("malloc eventData fail, fd:{},epollfd:{}", fd, efd);
        return BUS_ERROR;
    }

    evdata->data = data;
    evdata->handler = handler;
    evdata->fd = fd;

    eventsLock.lock();
    AddEvent(evdata);
    eventsLock.unlock();

    ev.data.ptr = evdata;
    BUSLOG_DEBUG("epoll add, fd:{},epollfd:{}", fd, efd);
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
    if (ret) {
        eventsLock.lock();
        DeleteEvent(fd);
        eventsLock.unlock();

        if (errno != EEXIST) {
            BUSLOG_ERROR("epoll add fail, fd:{},epollfd:{},errno:{}", fd, efd, errno);
        } else {
            BUSLOG_ERROR("epoll add already exists, fd:{},epollfd:{},errno:{}", fd, efd, errno);
        }
        return BUS_ERROR;
    }

    return BUS_OK;
}

int EvLoop::DelFdEvent(int fd)
{
    EventData *tev = nullptr;
    struct epoll_event ev;
    int ret;

    eventsLock.lock();
    tev = FindEvent(fd);
    if (tev == nullptr) {
        eventsLock.unlock();
        BUSLOG_DEBUG("event search fail, fd:{},epollfd:{}", fd, efd);
        return BUS_ERROR;
    }
    (void)events.erase(tev->fd);

    // Don't delete tev immediately, let's push it into deletedEvents, before next epoll_wait,we will free
    // all events in deletedEvents.
    AddDeletedEvents(tev);

    eventsLock.unlock();

    BUSLOG_DEBUG("epoll ctl delete, fd:{},epollfd:{}", fd, efd);
    ev.events = 0;
    ev.data.ptr = tev;
    ret = epoll_ctl(efd, EPOLL_CTL_DEL, fd, &ev);
    if (ret < 0) {
        BUSLOG_ERROR("epoll ctl delete fail, fd:{},epollfd:{},errno:{}", fd, efd, errno);
        return BUS_ERROR;
    }

    return BUS_OK;
}

int EvLoop::ModifyFdEvent(int fd, uint32_t tEvents)
{
    struct epoll_event ev;
    EventData *tev = nullptr;
    int ret;

    tev = FindEvent(fd);
    if (tev == nullptr) {
        BUSLOG_ERROR("event lookup fail, fd:{},events:{}", fd, tEvents);
        return BUS_ERROR;
    }

    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(&ev, sizeof(ev), 0, sizeof(ev));

    ev.events = tEvents;
    ev.data.ptr = tev;

    BUSLOG_DEBUG("epoll modify, fd:{},events:{}", fd, tEvents);
    ret = epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);
    if (ret != 0) {
        BUSLOG_ERROR("epoll modify fail, fd:{},events:{},errno:{}", fd, tEvents, errno);
        return BUS_ERROR;
    }

    return BUS_OK;
}

void EvLoop::AddDeletedEvents(EventData *eventData)
{
    // caller need check eventData is not nullptr
    std::list<EventData *> deleteEventList;

    // if fd not found, push eventData into deletedEvents[fd]
    std::map<int, std::list<EventData *>>::iterator fdIter = deletedEvents.find(eventData->fd);
    if (fdIter == deletedEvents.end()) {
        deletedEvents[eventData->fd].push_back(eventData);
        return;
    }

    // if fd found, check if same eventData ptr exists
    deleteEventList = fdIter->second;
    std::list<EventData *>::iterator eventIter = deleteEventList.begin();
    bool found = false;
    while (eventIter != deleteEventList.end()) {
        if (*eventIter == eventData) {
            BUSLOG_WARN("fd has been deleted before, fd:{},efd:{}", eventData->fd, efd);
            found = true;
            break;
        }
        ++eventIter;
    }

    // if found same eventData ptr, do nothing
    if (found) {
        return;
    }

    deletedEvents[eventData->fd].push_back(eventData);

    return;
}

void EvLoop::EventFreeDelEvents()
{
    std::map<int, std::list<EventData *>>::iterator fdIter = deletedEvents.begin();
    while (fdIter != deletedEvents.end()) {
        std::list<EventData *> deleteEventList = fdIter->second;
        std::list<EventData *>::iterator eventIter = deleteEventList.begin();
        while (eventIter != deleteEventList.end()) {
            EventData *deleteEv = *eventIter;
            delete deleteEv;
            deleteEv = nullptr;
            ++eventIter;
        }
        (void)deletedEvents.erase(fdIter++);
    }
    deletedEvents.clear();
}

int EvLoop::FindDeletedEvent(const EventData *tev)
{
    std::map<int, std::list<EventData *>>::iterator fdIter = deletedEvents.find(tev->fd);
    if (fdIter == deletedEvents.end()) {
        return 0;
    }

    std::list<EventData *> deleteEventList = fdIter->second;
    std::list<EventData *>::iterator eventIter = deleteEventList.begin();
    while (eventIter != deleteEventList.end()) {
        if (*eventIter == tev) {
            return 1;
        }
        ++eventIter;
    }
    return 0;
}

void EvLoop::HandleEvent(const struct epoll_event *tEvents, int nevent)
{
    int i;
    int found;
    EventData *tev = nullptr;

    for (i = 0; i < nevent; i++) {
        tev = static_cast<EventData *>(tEvents[i].data.ptr);
        if (tev != nullptr) {
            found = FindDeletedEvent(tev);
            if (found) {
                BUSLOG_WARN("fd has been deleted from epoll, fd:{},efd:{}", tev->fd, efd);
                continue;
            }

            tev->handler(tev->fd, tEvents[i].events, tev->data);
        }
    }
}

void EvLoop::StopEventLoop()
{
    if (stopLoop == 1) {
        return;
    }

    stopLoop = 1;

    uint64_t one = 1;
    if (write(queueEventfd, &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one))) {
        BUSLOG_WARN("fail to write queueEventfd, fd:{},errno:{}", queueEventfd, errno);
    }
    return;
}

void EvLoop::EventLoopDestroy()
{
    /* free deleted event handlers */
    EventFreeDelEvents();
    if (efd > 0) {
        if (queueEventfd > 0) {
            (void)DelFdEvent(queueEventfd);
            (void)close(queueEventfd);
            queueEventfd = -1;
        }

        (void)close(efd);
        efd = -1;
    }
}

}    // namespace litebus
