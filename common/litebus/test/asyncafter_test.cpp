#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/future.hpp"

#include "litebus.hpp"

using testing::_;
using testing::Return;
using testing::ReturnArg;

using litebus::ActorBase;
using litebus::AID;
using litebus::Async;
using litebus::AsyncAfter;
using litebus::Future;
using litebus::Timer;

static const std::string AsyncAfterActorName = "AsyncAfterActor";

struct MoveOnly {
    MoveOnly()
    {
    }

    MoveOnly(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&) = default;

    MoveOnly &operator=(const MoveOnly &) = delete;
    MoveOnly &operator=(MoveOnly &&) = default;
};

class AsyncAfterActor : public ActorBase {
public:
    AsyncAfterActor(const std::string &name) : ActorBase(name)
    {
    }

public:
    MOCK_METHOD0(func00, void());

    MOCK_METHOD1(func01, void(bool));
    MOCK_METHOD1(func02, void(Future<bool>));
    MOCK_METHOD1(func03, void(const bool &));
    MOCK_METHOD1(func04, void(const Future<bool> &));

    MOCK_METHOD2(func05, void(int, bool));
    MOCK_METHOD2(func06, void(Future<bool>, bool));
    MOCK_METHOD2(func07, void(const int &, const bool &));
    MOCK_METHOD2(func08, void(const Future<bool> &, const bool &));
};

class AsyncAfterTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("AsyncAfterTest SetUp");

        actor = std::make_shared<AsyncAfterActor>(AsyncAfterActorName);
        aid = litebus::Spawn(actor);
    }

    void TearDown()
    {
        BUSLOG_INFO("AsyncAfterTest TearDown");

        // finalize the litebus
        litebus::TerminateAll();
    }

public:
    std::shared_ptr<AsyncAfterActor> actor;
    AID aid;
};

TEST_F(AsyncAfterTest, THREADSAFE_AsyncAfter)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01(_));

    EXPECT_CALL(*actor, func02(_));

    EXPECT_CALL(*actor, func03(_));

    EXPECT_CALL(*actor, func04(_));

    EXPECT_CALL(*actor, func05(_, _));

    EXPECT_CALL(*actor, func06(_, _));

    EXPECT_CALL(*actor, func07(_, _));

    EXPECT_CALL(*actor, func08(_, _));

    Timer timer;
    timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func00);
    EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));

    timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func01, true);
    EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));

    timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func02, Future<bool>(true));
    EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));

    {
        bool param = true;
        timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func03, param);
        EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));
    }

    {
        Future<bool> param(true);
        timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func04, param);
        EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));
    }

    timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func05, 0, true);
    EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));

    timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func06, Future<bool>(true), true);
    EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));

    {
        bool param = true;
        timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func07, 0, param);
        EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));
    }

    {
        Future<bool> param(true);
        timer = AsyncAfter(100 /*ms*/, aid, &AsyncAfterActor::func08, param, true);
        EXPECT_EQ(std::string(aid), std::string(timer.GetTimerAID()));
    }

    EXPECT_TRUE(Future<bool>().WaitFor(1000 /*ms*/).IsError());
}
