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

#ifndef __LITEBUS_ASYNC_HPP__
#define __LITEBUS_ASYNC_HPP__

#include "actor/actor.hpp"
#include "actor/buslog.hpp"
#include "async/apply.hpp"
#include "async/future.hpp"

namespace litebus {

using MessageHandler = std::function<void(ActorBase *)>;

void Async(const AID &aid, std::unique_ptr<MessageHandler> handler);

namespace internal {

template <typename R>
struct AsyncHelper;

// for defer
template <>
struct AsyncHelper<void> {
    template <typename F>
    void operator()(const AID &aid, F &&f)
    {
        std::unique_ptr<std::function<void(ActorBase *)>> handler(
            new (std::nothrow) std::function<void(ActorBase *)>([=](ActorBase *) { f(); }));
        BUS_OOM_EXIT(handler);
        Async(aid, std::move(handler));
    }
};

template <typename R>
struct AsyncHelper<Future<R>> {
    template <typename F>
    Future<R> operator()(const AID &aid, F &&f)
    {
        std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
        BUS_OOM_EXIT(promise);
        Future<R> future = promise->GetFuture();

        std::unique_ptr<std::function<void(ActorBase *)>> handler(
            new (std::nothrow) std::function<void(ActorBase *)>([=](ActorBase *) { promise->Associate(f()); }));
        BUS_OOM_EXIT(handler);
        Async(aid, std::move(handler));
        return future;
    }
};

template <typename R>
struct AsyncHelper {
    template <typename F>
    Future<R> operator()(const AID &aid, F &&f)
    {
        std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
        BUS_OOM_EXIT(promise);
        Future<R> future = promise->GetFuture();

        std::unique_ptr<std::function<void(ActorBase *)>> handler(
            new (std::nothrow) std::function<void(ActorBase *)>([=](ActorBase *) { promise->SetValue(f()); }));
        BUS_OOM_EXIT(handler);
        Async(aid, std::move(handler));
        return future;
    }
};

}    // namespace internal

// return void
template <typename T>
void Async(const AID &aid, void (T::*method)())
{
    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([method](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            (t->*method)();
        }));
    BUS_OOM_EXIT(handler);
    Async(aid, std::move(handler));
}

template <typename T, typename Arg0, typename Arg1>
void Async(const AID &aid, void (T::*method)(Arg0), Arg1 &&arg)
{
    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([method, arg](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            (t->*method)(arg);
        }));
    BUS_OOM_EXIT(handler);
    Async(aid, std::move(handler));
}

template <typename T, typename... Args0, typename... Args1>
void Async(const AID &aid, void (T::*method)(Args0...), std::tuple<Args1...> &&tuple)
{
    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([method, tuple](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            Apply(t, method, tuple);
        }));
    BUS_OOM_EXIT(handler);
    Async(aid, std::move(handler));
}

template <typename T, typename... Args0, typename... Args1>
void Async(const AID &aid, void (T::*method)(Args0...), Args1 &&... args)
{
    auto tuple = std::make_tuple(std::forward<Args1>(args)...);
    Async(aid, method, std::move(tuple));
}

// return futrue
template <typename R, typename T>
Future<R> Async(const AID &aid, Future<R> (T::*method)())
{
    std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
    BUS_OOM_EXIT(promise);
    Future<R> future = promise->GetFuture();

    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([promise, method](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            promise->Associate((t->*method)());
        }));
    BUS_OOM_EXIT(handler);

    Async(aid, std::move(handler));
    return future;
}

template <typename R, typename T, typename Arg0, typename Arg1>
Future<R> Async(const AID &aid, Future<R> (T::*method)(Arg0), Arg1 &&arg)
{
    std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
    BUS_OOM_EXIT(promise);
    Future<R> future = promise->GetFuture();

    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([promise, method, arg](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            promise->Associate((t->*method)(arg));
        }));
    BUS_OOM_EXIT(handler);

    Async(aid, std::move(handler));
    return future;
}

template <typename R, typename T, typename... Args0, typename... Args1>
Future<R> Async(const AID &aid, Future<R> (T::*method)(Args0...), std::tuple<Args1...> &&tuple)
{
    std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
    BUS_OOM_EXIT(promise);
    Future<R> future = promise->GetFuture();

    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([promise, method, tuple](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            promise->Associate(Apply(t, method, tuple));
        }));
    BUS_OOM_EXIT(handler);

    Async(aid, std::move(handler));
    return future;
}

template <typename R, typename T, typename... Args0, typename... Args1>
Future<R> Async(const AID &aid, Future<R> (T::*method)(Args0...), Args1 &&... args)
{
    auto tuple = std::make_tuple(std::forward<Args1>(args)...);
    return Async(aid, method, std::move(tuple));
}

// return R
template <typename R, typename std::enable_if<!std::is_same<R, void>::value, int>::type = 0,
          typename std::enable_if<!internal::IsFuture<R>::value, int>::type = 0, typename T>
Future<R> Async(const AID &aid, R (T::*method)())
{
    std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
    BUS_OOM_EXIT(promise);
    Future<R> future = promise->GetFuture();

    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([promise, method](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            promise->SetValue((t->*method)());
        }));
    BUS_OOM_EXIT(handler);

    Async(aid, std::move(handler));
    return future;
}

template <typename R, typename std::enable_if<!std::is_same<R, void>::value, int>::type = 0,
          typename std::enable_if<!internal::IsFuture<R>::value, int>::type = 0, typename T, typename Arg0,
          typename Arg1>
Future<R> Async(const AID &aid, R (T::*method)(Arg0), Arg1 &&arg)
{
    std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
    BUS_OOM_EXIT(promise);
    Future<R> future = promise->GetFuture();

    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([promise, method, arg](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            promise->SetValue((t->*method)(arg));
        }));
    BUS_OOM_EXIT(handler);

    Async(aid, std::move(handler));
    return future;
}

template <typename R, typename std::enable_if<!std::is_same<R, void>::value, int>::type = 0,
          typename std::enable_if<!internal::IsFuture<R>::value, int>::type = 0, typename T, typename... Args0,
          typename... Args1>
Future<R> Async(const AID &aid, R (T::*method)(Args0...), std::tuple<Args1...> &&tuple)
{
    std::shared_ptr<Promise<R>> promise(std::make_shared<Promise<R>>());
    BUS_OOM_EXIT(promise);
    Future<R> future = promise->GetFuture();

    std::unique_ptr<std::function<void(ActorBase *)>> handler(
        new (std::nothrow) std::function<void(ActorBase *)>([promise, method, tuple](ActorBase *actor) {
            BUS_ASSERT(actor != nullptr);
            T *t = dynamic_cast<T *>(actor);
            BUS_ASSERT(t != nullptr);
            promise->SetValue(Apply(t, method, tuple));
        }));
    BUS_OOM_EXIT(handler);

    Async(aid, std::move(handler));
    return future;
}

template <typename R, typename std::enable_if<!std::is_same<R, void>::value, int>::type = 0,
          typename std::enable_if<!internal::IsFuture<R>::value, int>::type = 0, typename T, typename... Args0,
          typename... Args1>
Future<R> Async(const AID &aid, R (T::*method)(Args0...), Args1 &&... args)
{
    auto tuple = std::make_tuple(std::forward<Args1>(args)...);
    return Async(aid, method, std::move(tuple));
}

template <typename F, typename R = typename std::result_of<F()>::type>
auto Async(const AID &aid, F &&f) -> decltype(internal::AsyncHelper<R>()(aid, std::forward<F>(f)))
{
    return internal::AsyncHelper<R>()(aid, std::forward<F>(f));
}

}    // namespace litebus

#endif
