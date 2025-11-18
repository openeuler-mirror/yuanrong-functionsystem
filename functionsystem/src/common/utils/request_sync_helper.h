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

#ifndef COMMON_UTILS_REQUEST_SYNC_HELPER_H
#define COMMON_UTILS_REQUEST_SYNC_HELPER_H

#include <async/asyncafter.hpp>
#include <async/future.hpp>
#include <litebus.hpp>
#include <mutex>
#include <string>
#include <timer/timertools.hpp>
#include <unordered_map>

#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem {
template <typename T, typename Response>
class RequestSyncHelper {
public:
    RequestSyncHelper(litebus::ActorBase *actor, void (T::*asyncMethod)(const std::string &), const uint32_t &timeoutMs)
        : actor_(actor), asyncMethod_(asyncMethod), timeoutMs_(timeoutMs)
    {
    }

    ~RequestSyncHelper() noexcept
    {
        for (auto &iter : requestMatch_) {
            litebus::TimerTools::Cancel(iter.second.waitResponseTimer);
        }
        requestMatch_.clear();
    };

    litebus::Future<Response> AddSynchronizer(const std::string &key)
    {
        if (requestMatch_.find(key) != requestMatch_.end()) {
            litebus::TimerTools::Cancel(requestMatch_[key].waitResponseTimer);
        }
        Synchronizer rsp;
        requestMatch_[key] = std::move(rsp);
        rsp.waitResponseTimer = litebus::AsyncAfter(timeoutMs_, actor_->GetAID(), asyncMethod_, key);
        return requestMatch_[key].promise.GetFuture();
    }

    Status Synchronized(const std::string &key, const Response &rsp)
    {
        if (auto iter(requestMatch_.find(key)); iter == requestMatch_.end()) {
            return Status(StatusCode::FAILED);
        }
        requestMatch_[key].promise.SetValue(rsp);
        litebus::TimerTools::Cancel(requestMatch_[key].waitResponseTimer);
        requestMatch_.erase(key);
        return Status::OK();
    }

    void RequestTimeout(const std::string &key)
    {
        if (requestMatch_.find(key) == requestMatch_.end()) {
            return;
        }
        requestMatch_[key].promise.SetFailed(StatusCode::REQUEST_TIME_OUT);
        requestMatch_.erase(key);
    }

private:
    struct Synchronizer {
        litebus::Promise<Response> promise;
        litebus::Timer waitResponseTimer;
    };
    litebus::ActorBase *actor_;
    void (T::*asyncMethod_)(const std::string &);
    uint32_t timeoutMs_;
    std::unordered_map<std::string, Synchronizer> requestMatch_;
};

#define REQUEST_SYNC_HELPER(ActorClass, Response, TimeoutMs, member_)                                  \
    RequestSyncHelper<ActorClass, Response> member_{ this, &ActorClass::Timeout##member_, TimeoutMs }; \
    void Timeout##member_(const std::string &key)                                                      \
    {                                                                                                  \
        member_.RequestTimeout(key);                                                                   \
    }

template <typename T, typename Response>
class BackOffRetryHelper {
public:
    BackOffRetryHelper(litebus::ActorBase *actor,
                       void (T::*asyncMethod)(const std::string &key, const std::shared_ptr<litebus::AID> &to,
                                              std::string method, std::string msg, int64_t attempt))
        : actor_(actor), asyncMethod_(asyncMethod)
    {
    }

    ~BackOffRetryHelper() = default;

    void SetBackOffStrategy(std::function<int64_t(int64_t)> &&strategy, int64_t attemptLimit)
    {
        getBackOffMs_ = std::move(strategy);
        attemptLimit_ = attemptLimit;
    }

    litebus::Option<litebus::Future<Response>> Exist(const std::string &key)
    {
        if (requestMatch_.find(key) == requestMatch_.end()) {
            return litebus::None();
        }
        return requestMatch_[key].promise.GetFuture();
    }

    litebus::Future<Response> Begin(const std::string &key, const std::shared_ptr<litebus::AID> &to,
                                    std::string &&method, std::string &&msg)
    {
        Synchronizer sync;
        requestMatch_[key] = std::move(sync);
        auto future = requestMatch_[key].promise.GetFuture();
        (void)litebus::Async(actor_->GetAID(), asyncMethod_, key, to, std::move(method), std::move(msg), 1);
        return future;
    }

    void End(const std::string key, Response &&rsp)
    {
        if (requestMatch_.find(key) == requestMatch_.end()) {
            return;
        }
        requestMatch_[key].promise.SetValue(std::move(rsp));
        litebus::TimerTools::Cancel(requestMatch_[key].waitResponseTimer);
        requestMatch_.erase(key);
    }

    void AddTimer(const std::string &key, const std::shared_ptr<litebus::AID> &to, std::string method, std::string msg,
                  int64_t attempt)
    {
        if (requestMatch_.find(key) == requestMatch_.end()) {
            return;
        }
        ASSERT_IF_NULL(getBackOffMs_);
        requestMatch_[key].waitResponseTimer =
            litebus::AsyncAfter(getBackOffMs_(attempt), actor_->GetAID(), asyncMethod_, key, to, std::move(method),
                                std::move(msg), attempt);
    }

    void Failed(const std::string &key)
    {
        if (requestMatch_.find(key) == requestMatch_.end()) {
            return;
        }
        requestMatch_[key].promise.SetFailed(StatusCode::REQUEST_TIME_OUT);
        requestMatch_.erase(key);
    }

    bool ExceedAttemptLimit(const int64_t attempt) const
    {
        // if attemptLimit_ isn't set, retry endlessly
        if (attemptLimit_ == -1) {
            return false;
        }

        return attempt > attemptLimit_;
    }

    void EndAll(Response &rsp)
    {
        for (auto it = requestMatch_.begin(); it != requestMatch_.end();) {
            it->second.promise.SetValue(rsp);  // copy
            litebus::TimerTools::Cancel(it->second.waitResponseTimer);
            it = requestMatch_.erase(it);
        }
    }

private:
    struct Synchronizer {
        litebus::Promise<Response> promise;
        litebus::Timer waitResponseTimer;
    };
    litebus::ActorBase *actor_;
    void (T::*asyncMethod_)(const std::string &key, const std::shared_ptr<litebus::AID> &to, std::string method,
                            std::string msg, int64_t attempt);
    std::unordered_map<std::string, Synchronizer> requestMatch_;
    std::function<int64_t(int64_t)> getBackOffMs_ = nullptr;
    int64_t attemptLimit_ = -1;
};

#define BACK_OFF_RETRY_HELPER(ActorClass, Response, member_)                                                 \
    BackOffRetryHelper<ActorClass, Response> member_{ this, &ActorClass::Retry##member_ };                   \
    void Retry##member_(const std::string &key, const std::shared_ptr<litebus::AID> &to, std::string method, \
                        std::string msg, int64_t attempt)                                                    \
    {                                                                                                        \
        if (to == nullptr) {                                                                                 \
            YRLOG_ERROR("{}|Failed to send {}, aid is null", key, method);                                   \
            return;                                                                                          \
        }                                                                                                    \
                                                                                                             \
        if (member_.ExceedAttemptLimit(attempt)) {                                                           \
            YRLOG_ERROR("{}|Failed to send {} to {} for {} times", key, method, to->HashString(), attempt);  \
            member_.Failed(key);                                                                             \
        } else {                                                                                             \
            member_.AddTimer(key, to, method, msg, attempt + 1);                                             \
            YRLOG_DEBUG("{}|Send {} to {}; attempt: {}", key, method, to->HashString(), attempt);            \
            Send(*to, std::move(method), std::move(msg));                                                    \
        }                                                                                                    \
    }

}  // namespace functionsystem

#endif  // COMMON_UTILS_REQUEST_SYNC_HELPER_H
