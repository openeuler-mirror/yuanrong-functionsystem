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

#ifndef __LITEBUS_FUTURE_BASE_HPP__
#define __LITEBUS_FUTURE_BASE_HPP__

#include <future>
#include <iostream>
#include <list>

#include "actor/actor.hpp"
#include "actor/buslog.hpp"
#include "async/spinlock.hpp"
#include "async/status.hpp"

#include "timer/timertools.hpp"

namespace litebus {

template <typename T>
class Future;

template <typename T>
class Promise;

class LessFuture {
public:
    LessFuture() = default;
    LessFuture(const LessFuture &) = default;
    LessFuture &operator=(const LessFuture &) = delete;
    virtual ~LessFuture() = default;
};

class FutureBase : public LessFuture {
public:
    FutureBase() = default;
    FutureBase(const FutureBase &) = default;
    FutureBase &operator=(const FutureBase &) = delete;
    ~FutureBase() override = default;
};

template <typename T>
struct FutureData {
public:
    using CompleteCallback = std::function<void(const Future<T> &)>;
    using AbandonedCallback = std::function<void(const Future<T> &)>;

    FutureData()
        : status(Status::KINIT),
          associated(false),
          abandoned(false),
          gotten(false),
          promise(),
          future(promise.get_future()),
          t()
    {
    }

    ~FutureData()
    {
        try {
            Clear();
        } catch (...) {
            // Ignore
        }
    }

    // remove all callbacks
    void Clear()
    {
        onCompleteCallbacks.clear();
        onAbandonedCallbacks.clear();
    }

    // status of future
    SpinLock lock;
    Status status;

    bool associated;
    bool abandoned;
    bool gotten;

    std::promise<T> promise;

    // get from promise
    std::future<T> future;

    // complete callback
    std::list<CompleteCallback> onCompleteCallbacks;

    // abandoned callback
    std::list<AbandonedCallback> onAbandonedCallbacks;

    T t;
};

namespace internal {

const std::string WAIT_ACTOR_NAME = "WACTOR_";

class WaitActor : public ActorBase {
public:
    explicit WaitActor(const std::string &name) : litebus::ActorBase(name)
    {
    }

    ~WaitActor() override
    {
    }
};

template <typename T>
class DeferredHelper;

template <typename T>
struct Wrap {
    using type = Future<T>;
};

template <typename T>
struct Wrap<Future<T>> {
    using type = Future<T>;
};

template <typename T>
struct Unwrap {
    using type = T;
};

template <typename T>
struct Unwrap<Future<T>> {
    using type = T;
};

template <typename T>
struct IsFuture : public std::integral_constant<bool, std::is_base_of<FutureBase, T>::value> {};

template <typename H, typename... Args>
static void Run(std::list<H> &&handlers, Args &&...args)
{
    for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
        std::move (*iter)(std::forward<Args>(args)...);
    }
}

template <typename T>
static void Complete(const Future<T> &future, const Future<T> &f)
{
    if (f.IsError()) {
        future.SetFailed(f.GetErrorCode());
    } else if (f.IsOK()) {
        future.SetValue(f.Get());
    }
}

template <typename T>
static void Abandon(const Future<T> &future, bool abandon)
{
    future.Abandon(abandon);
}

template <typename T, typename R>
static void Thenf(const std::function<Future<R>(const T &)> &function, const std::shared_ptr<Promise<R>> &promise,
                  const Future<T> &f)
{
    if (f.IsError()) {
        promise->SetFailed(f.GetErrorCode());
    } else if (f.IsOK()) {
        promise->Associate(function(f.Get()));
    }
}

template <typename T, typename R>
static void Then(const std::function<R(const T &)> &function, const std::shared_ptr<Promise<R>> &promise,
                 const Future<T> &f)
{
    if (f.IsError()) {
        promise->SetFailed(f.GetErrorCode());
    } else if (f.IsOK()) {
        promise->SetValue(function(f.Get()));
    }
}

template <typename T>
static void Afterf(const std::function<Future<T>(const Future<T> &)> &f, const std::shared_ptr<Promise<T>> &promise,
                   const Future<T> &future)
{
    promise->Associate(f(future));
}

template <typename T>
static void After(const std::shared_ptr<Promise<T>> &promise, const litebus::Timer &timer, const Future<T> &future)
{
    (void)litebus::TimerTools::Cancel(timer);
    promise->Associate(future);
}

void Waitf(const AID &aid);

void Wait(const AID &aid, const litebus::Timer &timer);

}    // namespace internal

}    // namespace litebus

#endif
