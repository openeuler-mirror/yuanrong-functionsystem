#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/future.hpp"

#include "litebus.hpp"

using std::placeholders::_1;

using testing::_;
using testing::Return;
using testing::ReturnArg;

using litebus::ActorBase;
using litebus::AID;
using litebus::Async;
using litebus::Future;

static const std::string AsyncActorName = "AsyncActor";

struct MoveOnly {
    MoveOnly()
    {
    }

    MoveOnly(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&) = default;

    MoveOnly &operator=(const MoveOnly &) = delete;
    MoveOnly &operator=(MoveOnly &&) = default;
};

class AsyncActor : public ActorBase {
public:
    AsyncActor(const std::string &name) : ActorBase(name)
    {
    }

public:
    MOCK_METHOD0(func00, void());
    MOCK_METHOD0(func01, bool());
    MOCK_METHOD0(func02, Future<bool>());

    MOCK_METHOD1(func03, void(bool));
    MOCK_METHOD1(func04, bool(bool));
    MOCK_METHOD1(func05, Future<bool>(bool));

    MOCK_METHOD1(func06, void(Future<bool>));
    MOCK_METHOD1(func07, bool(Future<bool>));
    MOCK_METHOD1(func08, Future<bool>(Future<bool>));

    MOCK_METHOD1(func09, void(const bool &));
    MOCK_METHOD1(func10, bool(const bool &));
    MOCK_METHOD1(func11, Future<bool>(const bool &));

    MOCK_METHOD1(func12, void(const Future<bool> &));
    MOCK_METHOD1(func13, bool(const Future<bool> &));
    MOCK_METHOD1(func14, Future<bool>(const Future<bool> &));

    MOCK_METHOD2(func15, void(int, bool));
    MOCK_METHOD2(func16, bool(int, bool));
    MOCK_METHOD2(func17, Future<bool>(int, bool));

    MOCK_METHOD2(func18, void(Future<bool>, bool));
    MOCK_METHOD2(func19, bool(Future<bool>, bool));
    MOCK_METHOD2(func20, Future<bool>(Future<bool>, bool));

    MOCK_METHOD2(func21, void(const int &, const bool &));
    MOCK_METHOD2(func22, bool(const int &, const bool &));
    MOCK_METHOD2(func23, Future<bool>(const int &, const bool &));

    MOCK_METHOD2(func24, void(const Future<bool> &, const bool &));
    MOCK_METHOD2(func25, bool(const Future<bool> &, const bool &));
    MOCK_METHOD2(func26, Future<bool>(const Future<bool> &, const bool &));
};

class AsyncTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("AsyncTest SetUp");

        actor = std::make_shared<AsyncActor>(AsyncActorName);
        aid = litebus::Spawn(actor);
    }

    void TearDown()
    {
        BUSLOG_INFO("AsyncTest TearDown");

        // finalize the litebus
        litebus::TerminateAll();
    }

public:
    std::shared_ptr<AsyncActor> actor;
    AID aid;
};

static void AsyncVoidHandler()
{
}

static bool AsyncBoolHandler()
{
    return true;
}

static Future<bool> AsyncFutureHandler()
{
    return true;
}

static void OnComplete(const Future<bool> &future, bool *const check)
{
    ASSERT_TRUE(future.IsOK());
    *check = future.Get();
}

static void OnAbandoned(const Future<bool> &future, bool *const check)
{
    ASSERT_TRUE(future.IsInit());
    *check = true;
}

TEST_F(AsyncTest, THREADSAFE_Async)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func02()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func03(_));

    EXPECT_CALL(*actor, func04(_)).WillOnce(ReturnArg<0>());

    EXPECT_CALL(*actor, func05(_)).WillOnce(ReturnArg<0>());

    EXPECT_CALL(*actor, func06(_));

    EXPECT_CALL(*actor, func07(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func08(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func09(_));

    EXPECT_CALL(*actor, func10(_)).WillOnce(ReturnArg<0>());

    EXPECT_CALL(*actor, func11(_)).WillOnce(ReturnArg<0>());

    EXPECT_CALL(*actor, func12(_));

    EXPECT_CALL(*actor, func13(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func14(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func15(_, _));

    EXPECT_CALL(*actor, func16(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func17(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func18(_, _));

    EXPECT_CALL(*actor, func19(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func20(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func21(_, _));

    EXPECT_CALL(*actor, func22(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func23(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func24(_, _));

    EXPECT_CALL(*actor, func25(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func26(_, _)).WillOnce(ReturnArg<1>());

    Async(aid, &AsyncActor::func00);

    Future<bool> future;
    future = Async(aid, &AsyncActor::func01);
    EXPECT_TRUE(future.Get());

    future = Async(aid, &AsyncActor::func02);
    EXPECT_TRUE(future.Get());

    Async(aid, &AsyncActor::func03, true);

    future = Async(aid, &AsyncActor::func04, true);
    EXPECT_TRUE(future.Get());

    future = Async(aid, &AsyncActor::func05, true);
    EXPECT_TRUE(future.Get());

    Async(aid, &AsyncActor::func06, Future<bool>(true));

    {
        future = Async(aid, &AsyncActor::func07, Future<bool>(true));
        EXPECT_TRUE(future.Get());
    }

    {
        future = Async(aid, &AsyncActor::func08, Future<bool>(true));
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Async(aid, &AsyncActor::func09, param);
    }

    {
        bool param = true;
        future = Async(aid, &AsyncActor::func10, param);
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        future = Async(aid, &AsyncActor::func11, param);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Async(aid, &AsyncActor::func12, param);
    }

    {
        Future<bool> param(true);
        future = Async(aid, &AsyncActor::func13, param);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        future = Async(aid, &AsyncActor::func14, param);
        EXPECT_TRUE(future.Get());
    }

    Async(aid, &AsyncActor::func15, 0, true);

    future = Async(aid, &AsyncActor::func16, 0, true);
    EXPECT_TRUE(future.Get());

    future = Async(aid, &AsyncActor::func17, 0, true);
    EXPECT_TRUE(future.Get());

    Async(aid, &AsyncActor::func18, Future<bool>(true), true);

    {
        future = Async(aid, &AsyncActor::func19, Future<bool>(true), true);
        EXPECT_TRUE(future.Get());
    }

    {
        future = Async(aid, &AsyncActor::func20, Future<bool>(true), true);
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Async(aid, &AsyncActor::func21, 0, param);
    }

    {
        bool param = true;
        future = Async(aid, &AsyncActor::func22, 0, param);
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        future = Async(aid, &AsyncActor::func23, 0, param);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Async(aid, &AsyncActor::func24, param, true);
    }

    {
        Future<bool> param(true);
        future = Async(aid, &AsyncActor::func25, param, true);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        future = Async(aid, &AsyncActor::func26, param, true);
        EXPECT_TRUE(future.Get());
    }
}

TEST_F(AsyncTest, THREADSAFE_AsyncFunction)
{
    Async(aid, &AsyncVoidHandler);

    Future<bool> future;
    future = Async(aid, &AsyncBoolHandler);
    EXPECT_TRUE(future.Get());

    future = Async(aid, &AsyncFutureHandler);
    EXPECT_TRUE(future.Get());
}

TEST_F(AsyncTest, THREADSAFE_AsyncToValidAid)
{
    EXPECT_CALL(*actor, func26(_, _)).WillOnce(ReturnArg<1>());

    bool checkAbandoned = false;
    bool checkComplete = false;

    {
        Future<bool> param(true);
        Future<bool> future;
        future = Async(aid, &AsyncActor::func26, param, true)
                     .OnComplete(std::bind(&OnComplete, ::_1, &checkComplete))
                     .OnAbandoned(std::bind(&OnAbandoned, ::_1, &checkAbandoned));
    }

    Future<bool> f;
    f.WaitFor(100 /* ms */);

    EXPECT_FALSE(checkAbandoned);
    EXPECT_TRUE(checkComplete);
}

TEST_F(AsyncTest, THREADSAFE_AsyncToInvalidAid)
{
    bool checkAbandoned = false;
    bool checkComplete = false;

    {
        Future<bool> param(true);
        Future<bool> future;
        future = Async("", &AsyncActor::func26, param, true)
                     .OnComplete(std::bind(&OnComplete, ::_1, &checkComplete))
                     .OnAbandoned(std::bind(&OnAbandoned, ::_1, &checkAbandoned));
    }

    Future<bool> f;
    f.WaitFor(100 /* ms */);

    EXPECT_TRUE(checkAbandoned);
    EXPECT_FALSE(checkComplete);
}
