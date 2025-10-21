#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <signal.h>

#include <sys/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "async/common.hpp"
#include "async/defer.hpp"
#include "async/future.hpp"
#include "async/option.hpp"

#include "executils.hpp"
#include "litebus.hpp"
#include "utils/os_utils.hpp"

using litebus::ActorBase;
using litebus::ActorReference;
using litebus::AID;
using litebus::Collect;
using litebus::Defer;
using litebus::Future;
using litebus::Nothing;
using litebus::Option;
using litebus::Promise;
using litebus::SpinLock;

typedef struct timeval TimeVal;

static const std::string ASYNC_NAME = "AsyncActor";

static std::atomic_ulong g_handlerCount(0);

class Watch {
public:
    Watch()
    {
    }

    ~Watch() throw()
    {
    }

public:
    void Start()
    {
        gettimeofday(&bgnTimerVal, nullptr);
    }

    double Elapsed()
    {
        gettimeofday(&endTimerVal, nullptr);
        return (1000.0 * (endTimerVal.tv_sec - bgnTimerVal.tv_sec)
                + (endTimerVal.tv_usec - bgnTimerVal.tv_usec) / 1000.0);
    }

private:
    TimeVal bgnTimerVal;
    TimeVal endTimerVal;
};

class AsyncActor : public ActorBase {
public:
    AsyncActor(const std::string &name, const std::shared_ptr<Promise<Nothing>> &promise, size_t repeat)
        : ActorBase(name), promise(promise), repeat(repeat)
    {
    }

    virtual ~AsyncActor()
    {
    }

public:
    struct Movable {
        std::vector<int> data;
    };

    struct Copyable {
        std::vector<int> data;

        Copyable(std::vector<int> &&data) : data(std::move(data))
        {
        }

        Copyable(const Copyable &that) = default;
        Copyable &operator=(const Copyable &) = default;
    };

    template <typename T>
    Future<Nothing> Handler(const T &data)
    {
        ++count;

        if (count > repeat) {
            return Nothing();
        }

        if (count == repeat) {
            g_handlerCount.fetch_add(1);
            promise->SetValue(Nothing());
            return Nothing();
        }

        g_handlerCount.fetch_add(1);
        ;

        Async(this->GetAID(), &AsyncActor::HandlerNothing).Then(Defer(this->GetAID(), &AsyncActor::Handler<T>, data));

        return Nothing();
    }

    template <typename T>
    Future<Nothing> MultipleHandler(const std::vector<AID> &asyncAids, const T &data)
    {
        ++count;

        if (count > repeat) {
            return Nothing();
        }

        if (count == repeat) {
            g_handlerCount.fetch_add(1);
            promise->SetValue(Nothing());
            return Nothing();
        }

        g_handlerCount.fetch_add(1);

        for (size_t i = 0; i < asyncAids.size(); i++) {
            Async(this->GetAID(), &AsyncActor::HandlerNothing)
                .Then(Defer(asyncAids[i], &AsyncActor::MultipleHandler<T>, asyncAids, data));
        }

        return Nothing();
    }

    template <typename T>
    Future<Nothing> MultipleWaitHandler(const T &data, uint64_t timeMs)
    {
        Promise<bool> p;
        Future<bool> f = p.GetFuture();
        f.WaitFor(timeMs);
        promise->SetValue(Nothing());
        g_handlerCount.fetch_add(1);

        return Nothing();
    }

    template <typename T>
    static void Run(const std::string &name, size_t repeats)
    {
        g_handlerCount = 0;

        std::shared_ptr<Promise<Nothing>> promise(new Promise<Nothing>());
        ActorReference actor(new AsyncActor(ASYNC_NAME, promise, repeats));
        AID aid = litebus::Spawn(actor);

        T data = { std::vector<int>(10240, 42) };

        Watch watch;
        watch.Start();

        Async(aid, &AsyncActor::Handler<T>, data);

        promise->GetFuture().Wait();

        BUSLOG_INFO("{} elapsed: {} ms", name, watch.Elapsed());

        litebus::Terminate(aid);
        litebus::Await(aid);
    }

    template <typename T>
    static void MultipleRun(const std::string &name, size_t repeats, size_t actors)
    {
        g_handlerCount = 0;

        std::list<Future<Nothing>> futures;

        std::string asyncActorName;
        std::vector<AID> asyncAids;
        std::vector<ActorReference> asyncActors;

        for (size_t i = 0; i < actors; i++) {
            std::shared_ptr<Promise<Nothing>> promise(new Promise<Nothing>());
            futures.push_back(promise->GetFuture());

            asyncActorName = ASYNC_NAME + std::to_string(i);
            ActorReference asyncActor(new AsyncActor(asyncActorName, promise, repeats));
            asyncActors.push_back(asyncActor);
            asyncAids.push_back(litebus::Spawn(asyncActors.back()));
        }

        Future<std::list<Nothing>> collect = Collect(futures);

        T data = { std::vector<int>(10240, 42) };

        Watch watch;
        watch.Start();

        for (size_t i = 0; i < actors; i++) {
            Async(asyncAids[i], &AsyncActor::MultipleHandler<T>, asyncAids, data);
        }

        collect.Wait();

        BUSLOG_INFO("{} elapsed: {} ms", name, watch.Elapsed());

        litebus::TerminateAll();
    }

    template <typename T>
    static void MultipleWait(const std::string &name, size_t repeats, size_t actors, uint64_t timeMs)
    {
        g_handlerCount = 0;

        std::list<Future<Nothing>> futures;

        std::string asyncActorName;
        std::vector<AID> asyncAids;
        std::vector<ActorReference> asyncActors;

        for (size_t i = 0; i < actors; i++) {
            std::shared_ptr<Promise<Nothing>> promise(new Promise<Nothing>());
            futures.push_back(promise->GetFuture());

            asyncActorName = ASYNC_NAME + std::to_string(i);
            ActorReference asyncActor(new AsyncActor(asyncActorName, promise, repeats));
            asyncActors.push_back(asyncActor);
            asyncAids.push_back(litebus::Spawn(asyncActors.back()));
        }

        Future<std::list<Nothing>> collect = Collect(futures);

        T data = { std::vector<int>(10240, 42) };

        Watch watch;
        watch.Start();

        for (size_t i = 0; i < actors; i++) {
            Async(asyncAids[i], &AsyncActor::MultipleWaitHandler<T>, data, timeMs);
        }

        collect.Wait();

        BUSLOG_INFO("{} elapsed: {} ms", name, watch.Elapsed());

        litebus::TerminateAll();
    }

protected:
    virtual void Init()
    {
    }

private:
    Future<Nothing> HandlerNothing()
    {
        return Nothing();
    }

private:
    std::shared_ptr<Promise<Nothing>> promise;
    size_t repeat;
    size_t count = 0;
};

TEST(StabilityTest, AsyncRepeat)
{
    constexpr size_t repeats = 10000;

    AsyncActor::Run<AsyncActor::Movable>("Movable", repeats);
    EXPECT_EQ(g_handlerCount, repeats);

    AsyncActor::Run<AsyncActor::Copyable>("Copyable", repeats);
    EXPECT_EQ(g_handlerCount, repeats);
}

TEST(StabilityTest, MultipleAsyncRepeat)
{
    constexpr size_t repeats = 1000;
    constexpr size_t actors = 10;

    AsyncActor::MultipleRun<AsyncActor::Movable>("Movable", repeats, actors);
    EXPECT_EQ(g_handlerCount, repeats * actors);

    AsyncActor::MultipleRun<AsyncActor::Copyable>("Copyable", repeats, actors);
    EXPECT_EQ(g_handlerCount, repeats * actors);
}

TEST(StabilityTest, MultipleAsyncWait)
{
    constexpr size_t repeats = 0;
    constexpr size_t actors = 1000;
    constexpr size_t timeMs = 10;

    AsyncActor::MultipleWait<AsyncActor::Movable>("Movable", repeats, actors, timeMs);
    EXPECT_EQ(g_handlerCount, actors);

    AsyncActor::MultipleWait<AsyncActor::Copyable>("Copyable", repeats, actors, timeMs);
    EXPECT_EQ(g_handlerCount, actors);
}

int main(int argc, char **argv)
{
    // Initialize Google Test.
    testing::InitGoogleTest(&argc, argv);

    setenv("LITEBUS_THREADS", "100", false);

    int port = litebus::find_available_port();
    litebus::os::SetEnv("LITEBUS_PORT", std::to_string(port));
    int serverPort = litebus::find_available_port();
    litebus::os::SetEnv("API_SERVER_PORT", std::to_string(serverPort));
    litebus::Initialize("tcp://127.0.0.1:" + std::to_string(port));

    int result = RUN_ALL_TESTS();

    litebus::Finalize();

    return result;
}
