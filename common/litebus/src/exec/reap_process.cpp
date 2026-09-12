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

#include "exec/reap_process.hpp"
#include <sys/wait.h>
#include <set>
#include "actor/buslog.hpp"
#include "async/asyncafter.hpp"
#include "litebus.hpp"
#include <vector>

namespace litebus {
static const int REAP_INTERVAL = 200;

static const std::string REAPER_ACTOR_NAME = "ProcessReaperActor";

std::unordered_multimap<pid_t, std::shared_ptr<Promise<Option<int>>>> g_promises;

static std::mutex g_promisesLock{};

std::atomic_bool g_reapActor(false);

// current asyncafter timer is runing?
bool g_reaping = false;

AID g_reaperAID;

namespace reapinternal {

// pid exist?
inline bool PidExist(pid_t pid)
{
    return (::kill(pid, 0) == 0 || errno == EPERM);
}

// os wait pid
inline pid_t OSWaitPid(pid_t pid, int *status, int options)
{
    return (::waitpid(pid, status, options));
}
}    // namespace reapinternal

namespace {
using ReapPromises = std::vector<std::shared_ptr<Promise<Option<int>>>>;

// The caller holds g_promisesLock. Taking ownership before releasing the lock
// prevents another reaper from completing the same child with a missing status.
ReapPromises TakePromises(pid_t pid)
{
    ReapPromises promises;
    auto range = g_promises.equal_range(pid);
    for (auto it = range.first; it != range.second; ++it) {
        promises.push_back(it->second);
    }
    g_promises.erase(pid);
    return promises;
}

void CompletePromises(const ReapPromises &promises, pid_t pid, int result, int status)
{
    for (const auto &promise : promises) {
        if (result > 0) {
            BUSLOG_INFO("Notify pid:{},status:{}", pid, status);
            promise->SetValue(Option<int>(status));
        } else {
            BUSLOG_WARN("Notify pid failed:{},result:{}", pid, result);
            promise->SetFailed(result == 0 ? -1 : result);
        }
    }
}
}  // namespace

void NotifyPromise(pid_t pid, int result, int status)
{
    ReapPromises promises;
    {
        std::lock_guard<std::mutex> lock(g_promisesLock);
        promises = TakePromises(pid);
    }
    // Future callbacks may register another process. Never invoke them while
    // holding the registry lock.
    CompletePromises(promises, pid, result, status);
}

bool TryNotifyExternalReap(pid_t pid, int status)
{
    ReapPromises promises;
    {
        std::lock_guard<std::mutex> lock(g_promisesLock);
        promises = TakePromises(pid);
    }
    CompletePromises(promises, pid, pid, status);
    return !promises.empty();
}

pid_t ReapAnyChild(int &status, bool &notified)
{
    ReapPromises promises;
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(g_promisesLock);
        // waitpid consumes the kernel status. It must share the critical
        // section with promise transfer and ReaperActor's missing-pid check.
        pid = reapinternal::OSWaitPid(-1, &status, WNOHANG);
        if (pid > 0) {
            promises = TakePromises(pid);
        }
    }
    notified = !promises.empty();
    CompletePromises(promises, pid, pid, status);
    return pid;
}

ReaperActor::~ReaperActor()
{
}

void ReaperActor::Finalize()
{
    ReapStatus(false);
    std::lock_guard<std::mutex> lock(g_promisesLock);
    g_reapActor.store(false);
    g_reaping = false;
    BUSLOG_INFO("ReapActor Finalize");
    // to iterator pid in map
    for (auto it = g_promises.begin(); it != g_promises.end(); ++it) {
        // notify reaped
        it->second->SetValue(Option<int>(0));
    }
}

void ReaperActor::ReapStatus(bool withTimer)
{
    g_promisesLock.lock();

    std::set<pid_t> keySet;
    for (auto it = g_promises.begin(); it != g_promises.end(); ++it) {
        (void)keySet.insert(it->first);
    }
    g_promisesLock.unlock();
    for (const auto pid : keySet) {
        ReapPromises promises;
        int status = 0;
        pid_t childPid = 0;
        {
            std::lock_guard<std::mutex> lock(g_promisesLock);
            if (g_promises.find(pid) == g_promises.end()) {
                continue;
            }
            childPid = reapinternal::OSWaitPid(pid, &status, WNOHANG);
            if (childPid > 0 || !litebus::reapinternal::PidExist(pid)) {
                promises = TakePromises(pid);
            }
        }
        CompletePromises(promises, pid, childPid, status);
    }

    // if promises still has then wait for next time reap
    g_promisesLock.lock();
    if (g_promises.size() > 0) {
        // if with timer, need to reap next time
        if (withTimer) {
            (void)AsyncAfter(REAP_INTERVAL, g_reaperAID, &ReaperActor::ReapStatus, withTimer);
        }
    } else {
        // reap finshied, set the reapping flag to false;
        g_reaping = false;
        BUSLOG_INFO("All process reap finished.");
    }
    g_promisesLock.unlock();
    return;
};

// reap a pid with the new  thread(if all reap on one thread)
Future<Option<int>> ReapInActor(pid_t pid)
{
    // if pid exist then contiue to reap
    if (litebus::reapinternal::PidExist(pid)) {
        BUSLOG_INFO("Reap PID exist: {}", pid);
        std::shared_ptr<Promise<Option<int>>> promiseReaper(std::make_shared<Promise<Option<int>>>());
        BUS_OOM_EXIT(promiseReaper);
        Future<Option<int>> future = promiseReaper->GetFuture();
        std::lock_guard<std::mutex> lock(g_promisesLock);
        std::pair<pid_t, std::shared_ptr<Promise<Option<int>>>> promisePair(pid, promiseReaper);
        (void)g_promises.insert(promisePair);
        // create a actor to reap specify pid
        // only this is the firsttime reap/or all reap has been finished, then create a new thread
        if (!g_reapActor.load()) {
            g_reapActor.store(true);
            g_reaperAID = litebus::Spawn(std::make_shared<ReaperActor>(REAPER_ACTOR_NAME));
            BUSLOG_INFO("Create an actor to reap pid:{}", pid);
        }
        if (!g_reaping) {
            g_reaping = true;
            Async(g_reaperAID, &ReaperActor::ReapStatus, true);
            BUSLOG_INFO("Continue to reap pid:{}", pid);
        }
        return future;
    } else {
        // if a pid not exist then log and return;
        BUSLOG_ERROR("PID not exist:{}", pid);
        return None();
    }
}

}    // namespace litebus
