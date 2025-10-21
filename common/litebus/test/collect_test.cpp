#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/collect.hpp"
#include "async/defer.hpp"
#include "async/future.hpp"

#include "litebus.hpp"
#include <thread>

using std::placeholders::_1;

using testing::_;
using testing::Return;
using testing::ReturnArg;

using litebus::ActorBase;
using litebus::AID;
using litebus::Collect;
using litebus::Defer;
using litebus::Future;
using litebus::Promise;

static const int32_t KERROR = -1;
static const int32_t ERROR_CODE = -99;

static const std::string CollectActorName = "CollectActor";

static std::atomic_ulong g_handlerCount(0);

static bool func00(const Future<bool> &future)
{
    bool value = future.Get();
    g_handlerCount.fetch_add(1);
    return value;
}

static bool func01(const Future<std::list<bool>> &futures)
{
    bool value = true;

    std::list<bool> list = futures.Get();
    for (auto iter = list.begin(); iter != list.end(); ++iter) {
        if (!value) {
            value = false;
        }
    }

    g_handlerCount.fetch_add(1);
    return value;
}

class CollectActor : public ActorBase {
public:
    CollectActor(const std::string &name) : ActorBase(name)
    {
    }

    ~CollectActor()
    {
    }

public:
    bool func00()
    {
        return true;
    }

    bool func01(const Future<std::list<bool>> &futures)
    {
        bool value = true;

        std::list<bool> list = futures.Get();
        for (auto iter = list.begin(); iter != list.end(); ++iter) {
            if (!value) {
                value = false;
            }
        }

        return value;
    }

    void VisitAfterComplete(const Future<std::list<long>> &futures, const std::shared_ptr<Promise<long>> promise)
    {
        BUSLOG_INFO("futures:");
        std::list<long> list = futures.Get();
        int x = 0;
        for (auto i = list.begin(); i != list.end(); ++i) {
            BUSLOG_INFO("value: {}", *i);
            x += *i;
        }

        (void)promise->SetValue(x);
        BUSLOG_INFO("x: {}", x);
        Terminate();
    }

    void SetPromiseFail(Promise<long> tpromise)
    {
        tpromise.SetFailed(3);
    }
    void SetPromiseValue(Promise<long> tpromise)
    {
        tpromise.SetValue(3);
    }
    void SetPromise(Promise<long> tpromise)
    {
        auto v = rand() % 10;
        if (v > 2) {
            tpromise.SetValue(v);
        } else {
            tpromise.SetFailed(3);
        }
    }
};

class CollectTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("CollectTest SetUp");
    }

    void TearDown()
    {
        BUSLOG_INFO("CollectTest TearDown");
        // finalize the litebus
        litebus::Await("CollectTestAID");
        litebus::TerminateAll();
    }
};

class CollectDeferTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("CollectDeferTest SetUp");

        actor = std::make_shared<CollectActor>(CollectActorName);
        aid = litebus::Spawn(actor);
    }

    void TearDown()
    {
        BUSLOG_INFO("CollectDeferTest TearDown");

        // finalize the litebus
        litebus::TerminateAll();
    }

public:
    std::shared_ptr<CollectActor> actor;
    AID aid;
};

static Future<bool> HandleBatchJobSubmit(const litebus::AID &aid, size_t jobs)
{
    g_handlerCount = 0;
    std::list<Future<bool>> respList;

    for (size_t s = 0; s < jobs; s++) {
        Future<bool> resp = Async(aid, &CollectActor::func00);
        respList.push_back(resp);
    }

    return Collect(respList)
        .Then(Defer(aid, &CollectActor::func01, ::_1))
        .Then([]() -> Future<bool> { return true; })
        .Then(std::bind(func00, ::_1));
}

static Future<bool> HandleBatchJobSubmitStatic(const litebus::AID &aid, size_t jobs)
{
    g_handlerCount = 0;
    std::list<Future<bool>> respList;

    for (size_t s = 0; s < jobs; s++) {
        Future<bool> resp = Async(aid, &CollectActor::func00);
        respList.push_back(resp);
    }

    return Collect(respList)
        .Then(std::bind(func01, ::_1))
        .Then([]() -> Future<bool> { return true; })
        .Then(std::bind(func00, ::_1));
}

TEST_F(CollectDeferTest, THREADSAFE_CollectList)
{
    const size_t jobs = 10;

    Future<bool> future;

    future = HandleBatchJobSubmit(aid, jobs);
    EXPECT_TRUE(future.Get());
    EXPECT_EQ(g_handlerCount.load(), (unsigned long)1);

    future = HandleBatchJobSubmitStatic(aid, jobs);
    EXPECT_TRUE(future.Get());
    EXPECT_EQ(g_handlerCount.load(), (unsigned long)2);
}

TEST_F(CollectTest, CollectListCollectedComplete)
{
    std::list<Future<long>> empty;
    Future<std::list<long>> collect = Collect(empty);

    EXPECT_TRUE(collect.WaitFor(100).IsOK());
    Promise<long> tpromise1;
    Promise<long> tpromise2;
    Promise<long> tpromise3;
    Promise<long> tpromise4;

    std::list<Future<long>> futures;
    futures.push_back(tpromise1.GetFuture());
    futures.push_back(tpromise2.GetFuture());
    futures.push_back(tpromise3.GetFuture());
    futures.push_back(tpromise4.GetFuture());

    AID aid = litebus::Spawn(std::make_shared<CollectActor>("CollectTestAID"));
    AID aid1 = litebus::Spawn(std::make_shared<CollectActor>("CollectTestAID1"));
    AID aid2 = litebus::Spawn(std::make_shared<CollectActor>("CollectTestAID2"));
    AID aid3 = litebus::Spawn(std::make_shared<CollectActor>("CollectTestAID3"));
    AID aid4 = litebus::Spawn(std::make_shared<CollectActor>("CollectTestAID4"));

    std::shared_ptr<Promise<long>> promise(std::make_shared<Promise<long>>());

    Async(aid1, &CollectActor::SetPromise, tpromise1);
    Async(aid2, &CollectActor::SetPromise, tpromise2);
    Async(aid3, &CollectActor::SetPromise, tpromise3);
    Async(aid4, &CollectActor::SetPromise, tpromise4);

    collect = Collect<long>(futures);
    BUSLOG_INFO("step1");
    collect.OnComplete(Defer(aid, &CollectActor::VisitAfterComplete, ::_1, promise));
    BUSLOG_INFO("step2");
}

TEST_F(CollectTest, CollectList)
{
    std::list<Future<long>> empty;
    Future<std::list<long>> collect = Collect(empty);

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(collect.Get().empty());

    Promise<long> promise1;
    Promise<long> promise2;
    Promise<long> promise3;
    Promise<long> promise4;

    std::list<Future<long>> futures;
    futures.push_back(promise1.GetFuture());
    futures.push_back(promise2.GetFuture());
    futures.push_back(promise3.GetFuture());
    futures.push_back(promise4.GetFuture());

    collect = Collect(futures);

    promise4.SetValue(40000);
    promise2.SetValue(20000);
    promise1.SetValue(10000);
    promise3.SetValue(30000);

    collect.Wait();
    ASSERT_TRUE(collect.IsOK());

    std::list<long> values;
    values.push_back(10000);
    values.push_back(20000);
    values.push_back(30000);
    values.push_back(40000);

    EXPECT_EQ(values, collect.Get());
}

TEST_F(CollectTest, CollectListError)
{
    std::list<Future<long>> empty;
    Future<std::list<long>> collect = Collect(empty);

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsOK());

    Promise<long> promise1;
    Promise<long> promise2;
    Promise<long> promise3;
    Promise<long> promise4;

    std::list<Future<long>> futures;
    futures.push_back(promise1.GetFuture());
    futures.push_back(promise2.GetFuture());
    futures.push_back(promise3.GetFuture());
    futures.push_back(promise4.GetFuture());

    collect = Collect(futures);

    promise4.SetValue(40000);
    promise2.SetValue(20000);
    promise1.SetFailed(ERROR_CODE);
    promise3.SetValue(10000);

    collect.Wait();

    ASSERT_TRUE(collect.IsError());
    EXPECT_EQ(KERROR, promise3.GetFuture().GetErrorCode());
    EXPECT_EQ(ERROR_CODE, collect.GetErrorCode());
}

TEST_F(CollectTest, CollectListCollected)
{
    std::list<Future<long>> empty;
    Future<std::list<long>> collect = Collect(empty);

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsOK());

    Promise<long> promise1;
    Promise<long> promise2;
    Promise<long> promise3;
    Promise<long> promise4;

    std::list<Future<long>> futures;
    futures.push_back(promise1.GetFuture());
    futures.push_back(promise2.GetFuture());
    futures.push_back(promise3.GetFuture());
    futures.push_back(promise4.GetFuture());

    collect = Collect(futures);

    promise4.SetValue(80000);
    promise2.SetValue(60000);
    promise1.SetValue(50000);

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsError());

    std::list<long> values;
    values.push_back(10000);
    values.push_back(20000);
    values.push_back(30000);
    values.push_back(40000);

    collect.SetValue(values);
    collect.Wait();

    ASSERT_TRUE(collect.IsOK());
    EXPECT_EQ(values, collect.Get());
}

TEST_F(CollectTest, CollectListCollectedError)
{
    std::list<Future<long>> empty;
    Future<std::list<long>> collect = Collect(empty);

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsOK());

    Promise<long> promise1;
    Promise<long> promise2;
    Promise<long> promise3;
    Promise<long> promise4;

    std::list<Future<long>> futures;
    futures.push_back(promise1.GetFuture());
    futures.push_back(promise2.GetFuture());
    futures.push_back(promise3.GetFuture());
    futures.push_back(promise4.GetFuture());

    collect = Collect(futures);

    promise4.SetValue(80000);
    promise2.SetValue(60000);
    promise1.SetValue(50000);
    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsError());

    collect.SetFailed(ERROR_CODE);
    promise3.SetValue(70000);

    collect.Wait();

    ASSERT_TRUE(collect.IsError());
    EXPECT_EQ(KERROR, promise3.GetFuture().GetErrorCode());
    EXPECT_EQ(ERROR_CODE, collect.GetErrorCode());
}

TEST_F(CollectTest, CollectTuple)
{
    const long value = 42;
    Promise<long> promise1;
    Promise<bool> promise2;

    Future<std::tuple<long, bool>> collect = Collect(promise1.GetFuture(), promise2.GetFuture());

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsError());

    promise1.SetValue(value);
    ASSERT_TRUE(collect.IsInit());

    promise2.SetValue(true);
    ASSERT_TRUE(collect.IsOK());

    std::tuple<long, bool> values = collect.Get();

    ASSERT_EQ(value, std::get<0>(values));
    EXPECT_TRUE(std::get<1>(values));
}

TEST_F(CollectTest, CollectTupleError)
{
    const long value = 42;
    Promise<long> promise1;
    Promise<bool> promise2;

    Future<std::tuple<long, bool>> collect = Collect(promise1.GetFuture(), promise2.GetFuture());

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsError());

    promise1.SetValue(value);
    ASSERT_TRUE(collect.IsInit());

    promise2.SetFailed(ERROR_CODE);
    ASSERT_TRUE(collect.IsError());
    EXPECT_EQ(ERROR_CODE, collect.GetErrorCode());
}

TEST_F(CollectTest, CollectTupleCollected)
{
    const long value1 = 42;
    const long value2 = 43;
    Promise<long> promise1;
    Promise<bool> promise2;

    Future<std::tuple<long, bool>> collect = Collect(promise1.GetFuture(), promise2.GetFuture());

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsError());

    promise1.SetValue(value1);
    ASSERT_TRUE(collect.IsInit());

    std::tuple<long, bool> values = std::make_tuple(value2, true);
    collect.SetValue(values);
    promise2.SetValue(false);

    values = collect.Get();
    ASSERT_TRUE(collect.IsOK());

    EXPECT_EQ(value2, std::get<0>(values));
    EXPECT_TRUE(std::get<1>(values));
}

TEST_F(CollectTest, CollectTupleCollectedError)
{
    const long value = 42;
    Promise<long> promise1;
    Promise<bool> promise2;

    Future<std::tuple<long, bool>> collect = Collect(promise1.GetFuture(), promise2.GetFuture());

    EXPECT_TRUE(collect.WaitFor(100 /*ms*/).IsError());

    promise1.SetValue(value);
    ASSERT_TRUE(collect.IsInit());

    collect.SetFailed(ERROR_CODE);
    promise2.SetValue(true);

    ASSERT_TRUE(collect.IsError());
    EXPECT_EQ(ERROR_CODE, collect.GetErrorCode());
}
