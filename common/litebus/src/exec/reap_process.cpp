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

// get subprocess and write subprocess's status to promise
void NotifyPromise(pid_t pid, int result, int status)
{
    std::lock_guard<std::mutex> lock(g_promisesLock);
    // no pid found then return
    if (g_promises.find(pid) == g_promises.end()) {
        BUSLOG_ERROR("Map has no pid:{}", pid);
        return;
    }

    auto itRange = g_promises.equal_range(pid);
    // to iterator pid in map
    for (auto it = itRange.first; it != itRange.second; ++it) {
        // notify reaped
        if (result > 0) {
            BUSLOG_INFO("Notify pid:{},status:{}", pid, status);
            it->second->SetValue(Option<int>(status));
            // notify not exist
        } else if (result == 0) {
            BUSLOG_WARN("Notify pid none:{}", pid);
            it->second->SetFailed(0);
            // notify failed
        } else {
            BUSLOG_ERROR("Notify pid error:{}", pid);
            it->second->SetFailed(result);
        }
    }
    // remove pid from map finally
    (void)g_promises.erase(pid);
    return;
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
    // to loop all promise in map and find waitpid status
    for (auto keyIt = keySet.begin(); keyIt != keySet.end(); ++keyIt) {
        int status;
        pid_t childPid = 0;
        pid_t pid = *keyIt;
        // need to reap, involk os waitpid and get result
        childPid = litebus::reapinternal::OSWaitPid(pid, &status, WNOHANG);
        if (childPid > 0) {
            // We have reaped a sub process, wait and get status. notify status to
            // promise
            unsigned int st = static_cast<unsigned int>(status);
            BUSLOG_INFO("Reap success, pid:{},status:{},Wstatus:{}", pid, status, WEXITSTATUS(st));
            NotifyPromise(pid, childPid, status);
        } else {
            // pid still exist, need to reap again
            if (!litebus::reapinternal::PidExist(pid)) {
                // pid not exist, notify pid already exit
                BUSLOG_WARN("Reap pid not exist, result childpid:{},pid:{}", childPid, pid);
                // notify none to promise
                NotifyPromise(pid, 0, 0);
            }
        }
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
