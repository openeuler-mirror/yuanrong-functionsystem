#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "actor/actor.hpp"
#include "async/defer.hpp"
#include "async/future.hpp"

#include "litebus.hpp"

using std::placeholders::_1;
using std::placeholders::_2;

using testing::_;
using testing::Return;
using testing::ReturnArg;

using litebus::ActorBase;
using litebus::AID;
using litebus::Defer;
using litebus::Deferred;
using litebus::Future;

static const std::string DerferActorName = "DerferActor";

struct MoveOnly {
    MoveOnly()
    {
    }

    MoveOnly(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&) = default;

    MoveOnly &operator=(const MoveOnly &) = delete;
    MoveOnly &operator=(MoveOnly &&) = default;
};

class DerferActor : public ActorBase {
public:
    DerferActor(const std::string &name) : ActorBase(name)
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

    MOCK_METHOD2(func27, void(Future<bool>, bool));
    MOCK_METHOD2(func28, void(const int &, const bool &));
    MOCK_METHOD2(func29, void(const Future<bool> &, const bool &));
};

class DeferTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("DeferTest SetUp");

        actor = std::make_shared<DerferActor>(DerferActorName);
        aid = litebus::Spawn(actor);
    }

    void TearDown()
    {
        BUSLOG_INFO("DeferTest TearDown");

        // finalize the litebus
        litebus::TerminateAll();
    }

public:
    std::shared_ptr<DerferActor> actor;
    AID aid;
};

static void DeferVoidHandler(int, bool)
{
}

static bool DeferBoolHandler(int, bool)
{
    return true;
}

static Future<bool> DeferFutureHandler(int, bool)
{
    return true;
}

TEST_F(DeferTest, THREADSAFE_Deferred)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func02()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func03(_));

    EXPECT_CALL(*actor, func04(_)).WillRepeatedly(ReturnArg<0>());

    EXPECT_CALL(*actor, func05(_)).WillRepeatedly(ReturnArg<0>());

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

    EXPECT_CALL(*actor, func16(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func17(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func18(_, _));

    EXPECT_CALL(*actor, func19(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func20(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func21(_, _));

    EXPECT_CALL(*actor, func22(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func23(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func24(_, _));

    EXPECT_CALL(*actor, func25(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func26(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func27(_, _));

    EXPECT_CALL(*actor, func28(_, _));

    EXPECT_CALL(*actor, func29(_, _));

    {
        Deferred<void()> func00 = Defer(aid, &DerferActor::func00);
        func00();
    }

    Future<bool> future;

    {
        Deferred<Future<bool>()> func01 = Defer(aid, &DerferActor::func01);
        future = func01();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>()> func02 = Defer(aid, &DerferActor::func02);
        future = func02();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<void()> func03 = Defer(aid, &DerferActor::func03, true);
        func03();
    }

    {
        Deferred<Future<bool>()> func04 = Defer(aid, &DerferActor::func04, true);
        future = func04();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>(bool)> func04 = Defer(aid, &DerferActor::func04, ::_1);
        future = func04(true);
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>()> func05 = Defer(aid, &DerferActor::func05, true);
        future = func05();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>(bool)> func05 = Defer(aid, &DerferActor::func05, ::_1);
        future = func05(true);
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<void()> func06 = Defer(aid, &DerferActor::func06, Future<bool>(true));
        func06();
    }

    {
        Deferred<Future<bool>()> func07 = Defer(aid, &DerferActor::func07, Future<bool>(true));
        future = func07();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>()> func08 = Defer(aid, &DerferActor::func08, Future<bool>(true));
        future = func08();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Deferred<void()> func09 = Defer(aid, &DerferActor::func09, param);
        func09();
    }

    {
        bool param = true;
        Deferred<Future<bool>()> func10 = Defer(aid, &DerferActor::func10, param);
        future = func10();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Deferred<Future<bool>()> func11 = Defer(aid, &DerferActor::func11, param);
        future = func11();
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Deferred<void()> func12 = Defer(aid, &DerferActor::func12, param);
        func12();
    }

    {
        Future<bool> param(true);
        Deferred<Future<bool>()> func13 = Defer(aid, &DerferActor::func13, param);
        future = func13();
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Deferred<Future<bool>()> func14 = Defer(aid, &DerferActor::func14, param);
        future = func14();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<void()> func15 = Defer(aid, &DerferActor::func15, 0, true);
        func15();
    }

    {
        Deferred<Future<bool>()> func16 = Defer(aid, &DerferActor::func16, 0, true);
        future = func16();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>(bool)> func16 = Defer(aid, &DerferActor::func16, 0, ::_1);
        future = func16(true);
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>()> func17 = Defer(aid, &DerferActor::func17, 0, true);
        future = func17();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>(bool)> func17 = Defer(aid, &DerferActor::func17, 0, ::_1);
        future = func17(true);
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<void()> func18 = Defer(aid, &DerferActor::func18, Future<bool>(true), true);
        func18();
    }

    {
        Deferred<Future<bool>()> func19 = Defer(aid, &DerferActor::func19, Future<bool>(true), true);
        future = func19();
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<Future<bool>()> func20 = Defer(aid, &DerferActor::func20, Future<bool>(true), true);
        future = func20();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Deferred<void()> func21 = Defer(aid, &DerferActor::func21, 0, param);
        func21();
    }

    {
        bool param = true;
        Deferred<Future<bool>()> func22 = Defer(aid, &DerferActor::func22, 0, param);
        future = func22();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Deferred<Future<bool>(const bool &)> func22 = Defer(aid, &DerferActor::func22, 0, ::_1);
        future = func22(param);
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Deferred<Future<bool>()> func23 = Defer(aid, &DerferActor::func23, 0, param);
        future = func23();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        Deferred<Future<bool>(const int &)> func23 = Defer(aid, &DerferActor::func23, ::_1, param);
        future = func23(0);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Deferred<void()> func24 = Defer(aid, &DerferActor::func24, param, true);
        func24();
    }

    {
        Future<bool> param(true);
        Deferred<Future<bool>()> func25 = Defer(aid, &DerferActor::func25, param, true);
        future = func25();
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Deferred<Future<bool>(const Future<bool> &, const bool &)> func25 =
            Defer(aid, &DerferActor::func25, ::_1, ::_2);
        future = func25(param, true);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        Deferred<Future<bool>(const Future<bool> &, const bool &)> func26 =
            Defer(aid, &DerferActor::func26, ::_1, ::_2);
        future = func26(param, true);
        EXPECT_TRUE(future.Get());
    }

    {
        Deferred<void(Future<bool>)> func27 = Defer(aid, &DerferActor::func27, ::_1, true);
        func27(Future<bool>(true));
    }

    {
        Deferred<void(const int &, const bool &)> func28 = Defer(aid, &DerferActor::func28, ::_1, ::_2);
        func28(0, true);
    }

    {
        Future<bool> param(true);
        Deferred<void(const Future<bool> &)> func29 = Defer(aid, &DerferActor::func29, ::_1, true);
        func29(param);
    }
}

TEST_F(DeferTest, THREADSAFE_Function)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func02()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func03(_));

    EXPECT_CALL(*actor, func04(_)).WillRepeatedly(ReturnArg<0>());

    EXPECT_CALL(*actor, func05(_)).WillRepeatedly(ReturnArg<0>());

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

    EXPECT_CALL(*actor, func16(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func17(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func18(_, _));

    EXPECT_CALL(*actor, func19(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func20(_, _)).WillOnce(ReturnArg<1>());

    EXPECT_CALL(*actor, func21(_, _));

    EXPECT_CALL(*actor, func22(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func23(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func24(_, _));

    EXPECT_CALL(*actor, func25(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func26(_, _)).WillRepeatedly(ReturnArg<1>());

    EXPECT_CALL(*actor, func27(_, _));

    EXPECT_CALL(*actor, func28(_, _));

    EXPECT_CALL(*actor, func29(_, _));

    {
        std::function<void()> func00 = Defer(aid, &DerferActor::func00);
        func00();
    }

    Future<bool> future;

    {
        std::function<Future<bool>()> func01 = Defer(aid, &DerferActor::func01);
        future = func01();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>()> func02 = Defer(aid, &DerferActor::func02);
        future = func02();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<void()> func03 = Defer(aid, &DerferActor::func03, true);
        func03();
    }

    {
        std::function<Future<bool>()> func04 = Defer(aid, &DerferActor::func04, true);
        future = func04();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>(bool)> func04 = Defer(aid, &DerferActor::func04, ::_1);
        future = func04(true);
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>()> func05 = Defer(aid, &DerferActor::func05, true);
        future = func05();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>(bool)> func05 = Defer(aid, &DerferActor::func05, ::_1);
        future = func05(true);
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<void()> func06 = Defer(aid, &DerferActor::func06, Future<bool>(true));
        func06();
    }

    {
        std::function<Future<bool>()> func07 = Defer(aid, &DerferActor::func07, Future<bool>(true));
        future = func07();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>()> func08 = Defer(aid, &DerferActor::func08, Future<bool>(true));
        future = func08();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        std::function<void()> func09 = Defer(aid, &DerferActor::func09, param);
        func09();
    }

    {
        bool param = true;
        std::function<Future<bool>()> func10 = Defer(aid, &DerferActor::func10, param);
        future = func10();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        std::function<Future<bool>()> func11 = Defer(aid, &DerferActor::func11, param);
        future = func11();
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        std::function<void()> func12 = Defer(aid, &DerferActor::func12, param);
        func12();
    }

    {
        Future<bool> param(true);
        std::function<Future<bool>()> func13 = Defer(aid, &DerferActor::func13, param);
        future = func13();
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        std::function<Future<bool>()> func14 = Defer(aid, &DerferActor::func14, param);
        future = func14();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<void()> func15 = Defer(aid, &DerferActor::func15, 0, true);
        func15();
    }

    {
        std::function<Future<bool>()> func16 = Defer(aid, &DerferActor::func16, 0, true);
        future = func16();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>(bool)> func16 = Defer(aid, &DerferActor::func16, 0, ::_1);
        future = func16(true);
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>()> func17 = Defer(aid, &DerferActor::func17, 0, true);
        future = func17();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>(bool)> func17 = Defer(aid, &DerferActor::func17, 0, ::_1);
        future = func17(true);
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<void()> func18 = Defer(aid, &DerferActor::func18, Future<bool>(true), true);
        func18();
    }

    {
        std::function<Future<bool>()> func19 = Defer(aid, &DerferActor::func19, Future<bool>(true), true);
        future = func19();
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<Future<bool>()> func20 = Defer(aid, &DerferActor::func20, Future<bool>(true), true);
        future = func20();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        std::function<void()> func21 = Defer(aid, &DerferActor::func21, 0, param);
        func21();
    }

    {
        bool param = true;
        std::function<Future<bool>()> func22 = Defer(aid, &DerferActor::func22, 0, param);
        future = func22();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        std::function<Future<bool>(const bool &)> func22 = Defer(aid, &DerferActor::func22, 0, ::_1);
        future = func22(param);
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        std::function<Future<bool>()> func23 = Defer(aid, &DerferActor::func23, 0, param);
        future = func23();
        EXPECT_TRUE(future.Get());
    }

    {
        bool param = true;
        std::function<Future<bool>(const int &)> func23 = Defer(aid, &DerferActor::func23, ::_1, param);
        future = func23(0);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        std::function<void()> func24 = Defer(aid, &DerferActor::func24, param, true);
        func24();
    }

    {
        Future<bool> param(true);
        std::function<Future<bool>()> func25 = Defer(aid, &DerferActor::func25, param, true);
        future = func25();
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        std::function<Future<bool>(const Future<bool> &, const bool &)> func25 =
            Defer(aid, &DerferActor::func25, ::_1, ::_2);
        future = func25(param, true);
        EXPECT_TRUE(future.Get());
    }

    {
        Future<bool> param(true);
        std::function<Future<bool>(const Future<bool> &, const bool &)> func26 =
            Defer(aid, &DerferActor::func26, ::_1, ::_2);
        future = func26(param, true);
        EXPECT_TRUE(future.Get());
    }

    {
        std::function<void(Future<bool>)> func27 = Defer(aid, &DerferActor::func27, ::_1, true);
        func27(Future<bool>(true));
    }

    {
        std::function<void(const int &, const bool &)> func28 = Defer(aid, &DerferActor::func28, ::_1, ::_2);
        func28(0, true);
    }

    {
        Future<bool> param(true);
        std::function<void(const Future<bool> &)> func29 = Defer(aid, &DerferActor::func29, ::_1, true);
        func29(param);
    }
}

TEST_F(DeferTest, THREADSAFE_DeferDeferred)
{
    Deferred<void()> func00 = Defer(aid, std::bind(DeferVoidHandler, 0, true));
    func00();

    Deferred<void(bool)> func01 = Defer(aid, std::bind(DeferVoidHandler, 0, ::_1));
    func01(true);

    Deferred<void(int, bool)> func02 = Defer(aid, std::bind(DeferVoidHandler, ::_1, ::_2));
    func02(0, true);

    Future<bool> future;
    Deferred<Future<bool>()> func03 = Defer(aid, std::bind(DeferBoolHandler, 0, true));
    future = func03();
    EXPECT_TRUE(future.Get());

    Deferred<Future<bool>(bool)> func04 = Defer(aid, std::bind(DeferBoolHandler, 0, ::_1));
    future = func04(true);
    EXPECT_TRUE(future.Get());

    Deferred<Future<bool>(int, bool)> func05 = Defer(aid, std::bind(DeferBoolHandler, ::_1, ::_2));
    future = func05(0, true);
    EXPECT_TRUE(future.Get());

    Deferred<Future<bool>()> func06 = Defer(aid, std::bind(DeferFutureHandler, 0, true));
    future = func06();
    EXPECT_TRUE(future.Get());

    Deferred<Future<bool>(bool)> func07 = Defer(aid, std::bind(DeferFutureHandler, 0, ::_1));
    future = func07(true);
    EXPECT_TRUE(future.Get());

    Deferred<Future<bool>(int, bool)> func08 = Defer(aid, std::bind(DeferFutureHandler, ::_1, ::_2));
    future = func08(0, true);
    EXPECT_TRUE(future.Get());
}

TEST_F(DeferTest, THREADSAFE_DeferFunction)
{
    std::function<void()> func00 = Defer(aid, std::bind(DeferVoidHandler, 0, true));
    func00();

    std::function<void(bool)> func01 = Defer(aid, std::bind(DeferVoidHandler, 0, ::_1));
    func01(true);

    std::function<void(int, bool)> func02 = Defer(aid, std::bind(DeferVoidHandler, ::_1, ::_2));
    func02(0, true);

    Future<bool> future;
    std::function<Future<bool>()> func03 = Defer(aid, std::bind(DeferBoolHandler, 0, true));
    future = func03();
    EXPECT_TRUE(future.Get());

    std::function<Future<bool>(bool)> func04 = Defer(aid, std::bind(DeferBoolHandler, 0, ::_1));
    future = func04(true);
    EXPECT_TRUE(future.Get());

    std::function<Future<bool>(int, bool)> func05 = Defer(aid, std::bind(DeferBoolHandler, ::_1, ::_2));
    future = func05(0, true);
    EXPECT_TRUE(future.Get());

    std::function<Future<bool>()> func06 = Defer(aid, std::bind(DeferFutureHandler, 0, true));
    future = func06();
    EXPECT_TRUE(future.Get());

    std::function<Future<bool>(bool)> func07 = Defer(aid, std::bind(DeferFutureHandler, 0, ::_1));
    future = func07(true);
    EXPECT_TRUE(future.Get());

    std::function<Future<bool>(int, bool)> func08 = Defer(aid, std::bind(DeferFutureHandler, ::_1, ::_2));
    future = func08(0, true);
    EXPECT_TRUE(future.Get());
}
