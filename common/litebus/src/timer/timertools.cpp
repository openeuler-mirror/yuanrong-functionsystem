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

#include "timer/timertools.hpp"
#include <csignal>
#include <unistd.h>
#include <sys/timerfd.h>
#include "actor/buslog.hpp"
#include "evloop/evloop.hpp"

namespace litebus {
using TimerPoolType = std::map<Duration, std::list<Timer>>;
static std::unique_ptr<TimerPoolType> g_timerPool = nullptr;
static std::unique_ptr<EvLoop> g_timerEvLoop = nullptr;
static Duration g_ticks(0);
static int g_runTimerFD(-1);
static int g_watchTimerFD(-1);
static SpinLock g_timersLock{};
std::atomic_bool TimerTools::initStatus(false);
constexpr Duration SCAN_TIMERPOOL_DELAY = 30;
constexpr Duration WATCH_INTERVAL = 20;
constexpr unsigned int TIMER_LOG_INTERVAL = 6;
const static std::string TIMER_EVLOOP_THREADNAME = "HARES_LB_TMer";

namespace timer {
void ScanTimerPool(int fd, uint32_t events, void *data);
Duration NextTick(const std::map<Duration, std::list<Timer>> &timerPool)
{
    if (!timerPool.empty()) {
        Duration first = timerPool.begin()->first;
        return first;
    }
    return 0;
}

void ExecTimers(const std::list<Timer> &timers)
{
    for (const auto &timer : timers) {
        timer();
    }
}

void CreateTimerToLoop(const Duration &delay, const Duration &)
{
    if (g_runTimerFD == -1) {
        g_runTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (g_runTimerFD >= 0) {
            int retval =
                g_timerEvLoop->AddFdEvent(g_runTimerFD, static_cast<unsigned int>(EPOLLIN), ScanTimerPool, nullptr);
            if (retval != BUS_OK) {
                BUSLOG_ERROR("add run timer event fail, ID:{}", g_runTimerFD);
                (void)close(g_runTimerFD);
                g_runTimerFD = -1;
                return;
            }
        } else {
            BUSLOG_ERROR("create run timer fd fail, ID:{}", g_runTimerFD);
            g_runTimerFD = -1;
            return;
        }
        BUSLOG_INFO("create run timer fd success, ID:{}", g_runTimerFD);
    }

    struct itimerspec it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_nsec = 0;
    it.it_value.tv_sec = delay / SECTOMILLI;
    it.it_value.tv_nsec = (delay % SECTOMILLI) * MILLITOMICR * MICRTONANO;
    if (timerfd_settime(g_runTimerFD, 0, &it, nullptr) == -1) {
        BUSLOG_ERROR("start run timer fail, ID:{}", g_runTimerFD);
        (void)close(g_runTimerFD);
        g_runTimerFD = -1;
        return;
    }
}

void ScheduleTick(const std::map<Duration, std::list<Timer>> &timerPool)
{
    Duration nextTick = NextTick(timerPool);
    if (nextTick != 0) {
        // 'tick' scheduled for an earlier time, not schedule current 'tick'
        if ((g_ticks == 0) || (nextTick < g_ticks)) {
            Duration nowTime = TimeWatch::Now();
            Duration delay = 0;

            if (nextTick > nowTime) {
                delay = nextTick - nowTime;
                g_ticks = nextTick;
                CreateTimerToLoop(delay, nextTick);
            } else {
                delay = SCAN_TIMERPOOL_DELAY;
                g_ticks = delay + nowTime;
                CreateTimerToLoop(delay, nextTick);
                BUSLOG_DEBUG("run timer immediately (nextTick, now time)=({}, {})", nextTick, nowTime);
            }
        }
    }
}

// select timeout timers
void ScanTimerPool(int fd, uint32_t events, void *)
{
    std::list<Timer> outTimer;
    uint64_t count;

    if ((g_runTimerFD != fd) || !(events & static_cast<unsigned int>(EPOLLIN))) {
        BUSLOG_INFO("run timer fd or events err (g_runTimerFD, fd, events)=({}, {}, {})", g_runTimerFD, fd, events);
        return;
    }

    if (read(fd, &count, sizeof(uint64_t)) < 0) {
        return;
    }
    g_timersLock.Lock();
    Duration now = TimeWatch::Now();
    auto it = g_timerPool->begin();
    while (it != g_timerPool->end()) {
        if (it->first > now) {
            break;
        }
        outTimer.splice(outTimer.end(), (*g_timerPool)[it->first]);
        ++it;
    }
    // delete timed out timer
    (void)g_timerPool->erase(g_timerPool->begin(), g_timerPool->upper_bound(now));
    g_ticks = 0;
    ScheduleTick(*g_timerPool);
    g_timersLock.Unlock();

    ExecTimers(outTimer);
    outTimer.clear();
}

void CheckPassedTimer(int fd, uint32_t events, void *)
{
    std::list<Timer> passTimer;
    static unsigned long watchTimes = 0;
    uint64_t count;

    if ((g_watchTimerFD != fd) || !(events & static_cast<unsigned int>(EPOLLIN))) {
        BUSLOG_INFO("check timer fd or events err (g_watchTimerFD, fd, events)=({}, {}, {})", g_watchTimerFD, fd,
                    events);
        return;
    }
    if (read(fd, &count, sizeof(uint64_t)) < 0) {
        return;
    }
    g_timersLock.Lock();
    Duration now = TimeWatch::Now();
    ++watchTimes;

    for (auto it = g_timerPool->begin(); it != g_timerPool->end(); ++it) {
        if (it->first > now) {
            break;
        }
        passTimer.splice(passTimer.end(), (*g_timerPool)[it->first]);
    }
    // delete passed timer
    if (passTimer.size() > 0) {
        BUSLOG_DEBUG("fire pass timer (pass size, now, g_ticks)=({}, {}, {})", passTimer.size(), now, g_ticks);
    }
    (void)g_timerPool->erase(g_timerPool->begin(), g_timerPool->upper_bound(now));
    if (g_ticks <= now) {
        g_ticks = 0;
    }

    if (g_timerPool->size() > 0) {
        if ((watchTimes % TIMER_LOG_INTERVAL == 0) && (passTimer.size() > 0)
            && (now - g_timerPool->begin()->first > SECTOMILLI)) {
            BUSLOG_DEBUG(
                "timer info (pool size, pass size, now, g_ticks, poolTick, watchTimes)=({}, {}, {}, {}, {} ,{})",
                g_timerPool->size(), passTimer.size(), now, g_ticks, g_timerPool->begin()->first, watchTimes);
        }
    }

    BUSLOG_DEBUG("timer info (pool size, pass size, now, g_ticks, poolTick, watchTimes)=({}, {}, {}, {}, {}, {})",
                 g_timerPool->size(), passTimer.size(), now, g_ticks, g_timerPool->begin()->first, watchTimes);

    ScheduleTick(*g_timerPool);
    g_timersLock.Unlock();

    ExecTimers(passTimer);
    passTimer.clear();
}

bool StartWatchTimer()
{
    // create watch timer
    g_watchTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (g_watchTimerFD >= 0) {
        int retval =
            g_timerEvLoop->AddFdEvent(g_watchTimerFD, static_cast<unsigned int>(EPOLLIN), CheckPassedTimer, nullptr);
        if (retval != BUS_OK) {
            BUSLOG_ERROR("add watch timer event fail, ID:{}", g_watchTimerFD);
            (void)close(g_watchTimerFD);
            g_watchTimerFD = -1;
            return false;
        }
    } else {
        g_watchTimerFD = -1;
        return false;
    }

    // start watch timer
    struct itimerspec it;
    it.it_interval.tv_sec = WATCH_INTERVAL;
    it.it_interval.tv_nsec = 0;
    it.it_value.tv_sec = WATCH_INTERVAL;
    it.it_value.tv_nsec = 0;
    if (timerfd_settime(g_watchTimerFD, 0, &it, nullptr) == -1) {
        BUSLOG_ERROR("start watch timer fail, ID:{}", g_watchTimerFD);
        (void)close(g_watchTimerFD);
        g_watchTimerFD = -1;
        return false;
    }
    BUSLOG_INFO("start watch timer success, id={}", g_watchTimerFD);
    return true;
}
}    // namespace timer

bool TimerTools::Initialize()
{
    bool ret = true;
    g_timersLock.Lock();

    g_timerPool.reset(new (std::nothrow) TimerPoolType());
    if (g_timerPool == nullptr) {
        BUSLOG_ERROR("timer pool new failed.");
        g_timersLock.Unlock();
        return false;
    }

    g_timerEvLoop.reset(new (std::nothrow) EvLoop());
    if (g_timerEvLoop == nullptr) {
        BUSLOG_ERROR("ev new failed.");
        g_timersLock.Unlock();
        return false;
    }
    bool ok = g_timerEvLoop->Init(TIMER_EVLOOP_THREADNAME);
    if (!ok) {
        BUSLOG_ERROR("ev init failed.");
        g_timerEvLoop = nullptr;
        g_timersLock.Unlock();
        return false;
    }
    ret = timer::StartWatchTimer();
    g_timersLock.Unlock();
    initStatus.store(true);
    BUSLOG_INFO("Timer init succ.");
    return ret;
}

void TimerTools::Finalize()
{
    if (initStatus.load() == false) {
        BUSLOG_INFO("no need for Timer Finalize.");
        return;
    }
    initStatus.store(false);

    BUSLOG_INFO("Timer Finalize.");
    g_timersLock.Lock();
    if (g_timerEvLoop != nullptr) {
        (void)g_timerEvLoop->DelFdEvent(g_watchTimerFD);
        (void)g_timerEvLoop->DelFdEvent(g_runTimerFD);
        g_timerEvLoop->Finish();
        g_timerEvLoop = nullptr;
    }
    if (g_runTimerFD >= 0) {
        (void)close(g_runTimerFD);
        BUSLOG_INFO("run timer close ID={}", g_runTimerFD);
        g_runTimerFD = -1;
    }
    if (g_watchTimerFD >= 0) {
        (void)close(g_watchTimerFD);
        BUSLOG_INFO("watch timer close ID={}", g_watchTimerFD);
        g_watchTimerFD = -1;
    }
    g_timersLock.Unlock();
}

Timer TimerTools::AddTimer(const Duration &duration, const AID &aid, const std::function<void()> &thunk)
{
    if (initStatus.load() == false) {
        return Timer();
    }
    if (duration == 0) {
        thunk();
        return Timer();
    }
    static std::atomic<uint64_t> id(1);
    TimeWatch timeWatch = TimeWatch::In(duration);
    Timer timer(id.fetch_add(1), timeWatch, aid, thunk);

    // Add the timer to timerpoll and Schedule it
    g_timersLock.Lock();

    if (g_timerPool->size() == 0 || timer.GetTimeWatch().Time() < g_timerPool->begin()->first) {
        (*g_timerPool)[timer.GetTimeWatch().Time()].push_back(timer);
        timer::ScheduleTick(*g_timerPool);
    } else {
        if (!(g_timerPool->size() >= 1)) {
            BUSLOG_ERROR("g_timerPool size invalid size={}", g_timerPool->size());
        }
        (*g_timerPool)[timer.GetTimeWatch().Time()].push_back(timer);
    }
    g_timersLock.Unlock();

    return timer;
}

bool TimerTools::Cancel(const Timer &timer)
{
    if (initStatus.load() == false) {
        return false;
    }

    bool canceled = false;
    g_timersLock.Lock();
    Duration duration = timer.GetTimeWatch().Time();
    if (g_timerPool->count(duration) > 0) {
        canceled = true;
        (*g_timerPool)[duration].remove(timer);
        if ((*g_timerPool)[duration].empty()) {
            (void)(g_timerPool->erase(duration));
        }
    }
    g_timersLock.Unlock();

    return canceled;
}
}    // namespace litebus
