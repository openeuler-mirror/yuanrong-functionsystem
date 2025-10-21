#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/defer.hpp"
#include "async/future.hpp"
#include "async/option.hpp"

#include "litebus.hpp"

using std::placeholders::_1;

using testing::_;
using testing::Return;
using testing::ReturnArg;

using litebus::ActorBase;
using litebus::AID;
using litebus::Defer;
using litebus::Future;
using litebus::Option;
using litebus::Promise;

static const std::string FutureActorName = "FutureActor";

static const int32_t KERROR = -1;
static const int32_t ERROR_CODE = -99;

class FutureActor : public ActorBase {
public:
    FutureActor(const std::string &name) : ActorBase(name)
    {
    }

public:
    MOCK_METHOD0(func00, void());
    MOCK_METHOD1(func01, void(const Future<bool> &));
    MOCK_METHOD2(func02, void(const Future<bool> &, const bool &));
    MOCK_METHOD3(func03, void(const Future<bool> &, const bool &, const int &));

    MOCK_METHOD0(func04, bool());
    MOCK_METHOD1(func05, bool(const Future<bool> &));
    MOCK_METHOD2(func06, bool(const Future<bool> &, const bool &));
    MOCK_METHOD3(func07, bool(const Future<bool> &, const bool &, const int &));

    MOCK_METHOD0(func08, Future<bool>());
    MOCK_METHOD1(func09, Future<bool>(const Future<bool> &));
    MOCK_METHOD2(func10, Future<bool>(const Future<bool> &, const bool &));
    MOCK_METHOD3(func11, Future<bool>(const Future<bool> &, const bool &, const int &));

    void func12(const Promise<bool> &promise)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        promise.SetValue(true);
        promise.SetFailed(ERROR_CODE);
    }

    void func13(const Promise<bool> &promise)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        promise.SetFailed(ERROR_CODE);
        promise.SetValue(true);
    }

    void func14(const Future<bool> &future)
    {
        EXPECT_TRUE(future.IsError());
        BUSLOG_INFO("future is error and get value: {}", future.Get());
    }

    void func15(const Future<bool> &future, bool *const check)
    {
        *check = true;
    }
};

class FutureDeferTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("FutureDeferTest SetUp");

        actor = std::make_shared<FutureActor>(FutureActorName);
        aid = litebus::Spawn(actor);
    }

    void TearDown()
    {
        BUSLOG_INFO("FutureDeferTest TearDown");

        // finalize the litebus
        litebus::TerminateAll();
    }

public:
    std::shared_ptr<FutureActor> actor;
    AID aid;
};

static void OnComplete(const Future<bool> &future, bool *const check)
{
    ASSERT_TRUE(future.IsOK());
    *check = future.Get();
}

static void OnCompleteInit(const Future<bool> &future, bool *const check)
{
    ASSERT_TRUE(future.IsInit());
    *check = true;
}

static void OnAbandoned(const Future<bool> &future, bool *const check)
{
    ASSERT_TRUE(future.IsInit());
    *check = true;
}

static void OnCompleteError(const Future<bool> &future, int32_t *const check)
{
    ASSERT_TRUE(future.IsError());
    *check = future.GetErrorCode();
}

static std::string tostring()
{
    std::ostringstream out;
    out << "42";
    return out.str();
}

static Future<std::string> tofuture()
{
    std::ostringstream out;
    out << "42";
    return out.str();
}

static std::string itoa_string(int *const &i)
{
    std::ostringstream out;
    out << *i;
    return out.str();
}

static Future<std::string> itoa_future(int *const &i)
{
    std::ostringstream out;
    out << *i;
    return out.str();
}

static Future<int> after(std::atomic_bool *executed, const Future<int> &future)
{
    executed->store(true);

    std::shared_ptr<Promise<int>> promise(new Promise<int>());

    if (future.IsOK()) {
        promise->SetValue(future.Get());
    } else if (future.IsError()) {
        promise->SetFailed(future.GetErrorCode());
    }

    return promise->GetFuture();
}

static Future<bool> readyFuture()
{
    return true;
}

static Future<bool> failedFuture()
{
    return litebus::Status(ERROR_CODE);
}

static Future<bool> pendingFuture(const Future<bool> &future)
{
    return future;
}

static bool first()
{
    return true;
}

static Future<std::string> second(const bool &b)
{
    return b ? "true" : "false";
}

static Future<std::string> third(const std::string &s)
{
    return "(" + s + ")";
}

static bool func00()
{
    return true;
}

static bool func01(const Future<bool> &)
{
    return true;
}

static bool func02(const Future<bool> &, const bool &)
{
    return true;
}

static bool func03(const Future<bool> &, const bool &, const int &)
{
    return true;
}

TEST(FutureTest, Future)
{
    const int32_t code = ERROR_CODE;
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    promise.SetFailed(code);
    future.Clear();

    EXPECT_TRUE(future.Valid());
    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
    EXPECT_EQ(0, future.GetErrorCode());
}

TEST(FutureTest, FutureOK)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    future.SetOK();
    future.Clear();

    EXPECT_TRUE(future.Valid());
    ASSERT_TRUE(future.IsOK());
    EXPECT_FALSE(future.Get());
    EXPECT_EQ(0, future.GetErrorCode());
}

TEST(FutureTest, FutureError)
{
    const int32_t code = ERROR_CODE;
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(code);
    promise.SetValue(true);
    future.Clear();

    EXPECT_TRUE(future.Valid());
    ASSERT_TRUE(future.IsError());
    BUSLOG_INFO("future is error and get value: {}", future.Get());
    EXPECT_EQ(code, future.GetErrorCode());
}

TEST(FutureTest, Construct)
{
    Future<bool> future(true);
    future.SetValue(false);

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
    EXPECT_TRUE(future.Get());
}

TEST(FutureTest, ConstructError)
{
    litebus::Status status(ERROR_CODE);
    Future<bool> future(status);

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), status.GetCode());
    EXPECT_EQ(future.GetStatus().GetCode(), status.GetCode());
}

TEST(FutureTest, ConstructPtr)
{
    Future<bool> *future = new Future<bool>(true);
    future->SetValue(false);

    ASSERT_TRUE(future->IsOK());
    EXPECT_TRUE(future->Get());
    EXPECT_TRUE(future->Get());

    delete future;
}

TEST(FutureTest, ConstructPtrError)
{
    litebus::Status status(ERROR_CODE);
    Future<bool> *future = new Future<bool>(status);

    ASSERT_TRUE(future->IsError());
    EXPECT_EQ(future->GetErrorCode(), status.GetCode());
    EXPECT_EQ(future->GetStatus().GetCode(), status.GetCode());

    delete future;
}

TEST(FutureTest, Get)
{
    Future<bool> future;
    {
        std::shared_ptr<Promise<bool>> promise(new Promise<bool>());
        future = promise->GetFuture();
    }

    EXPECT_TRUE(future.WaitFor(100 /*ms*/).IsError());
}

TEST(FutureTest, SetValue)
{
    Future<bool> future;
    {
        std::shared_ptr<Promise<bool>> promise(new Promise<bool>());
        future = promise->GetFuture();
    }

    EXPECT_TRUE(future.WaitFor(100 /*ms*/).IsError());

    future.SetValue(true);

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
    EXPECT_TRUE(future.Get());
}

TEST(FutureTest, SetValueFuture)
{
    Future<bool> future;
    std::shared_ptr<Promise<bool>> promise(new Promise<bool>());
    future = promise->GetFuture();

    EXPECT_TRUE(future.WaitFor(100 /*ms*/).IsError());

    std::shared_ptr<Promise<bool>> p(new Promise<bool>());
    Future<bool> f = p->GetFuture();
    promise->SetValue(f);
    p->SetValue(true);

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
    EXPECT_TRUE(future.Get());
}

TEST(FutureTest, SetFailed)
{
    Future<bool> future;
    {
        std::shared_ptr<Promise<bool>> promise(new Promise<bool>());
        future = promise->GetFuture();
    }

    EXPECT_TRUE(future.WaitFor(100 /*ms*/).IsError());

    future.SetFailed(ERROR_CODE);

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
    EXPECT_EQ(ERROR_CODE, future.GetStatus().GetCode());
}

TEST(FutureTest, Abandon)
{
    bool check = false;
    Future<bool> f;
    Future<bool> future(f);
    future.OnComplete(std::bind(&OnCompleteInit, ::_1, &check));
    future.Abandon();

    ASSERT_TRUE(future.IsInit());
    EXPECT_FALSE(check);
}

TEST(FutureTest, AbandonFuture)
{
    bool check = false;

    {
        Promise<bool> promise;
        Future<bool> future = promise.GetFuture();
        ASSERT_TRUE(future.IsInit());
        future.OnAbandoned(std::bind(&OnAbandoned, ::_1, &check));
    }

    EXPECT_TRUE(check);
}

TEST(FutureTest, AbandonCompleteFuture)
{
    bool checkAbandoned = false;
    bool checkComplete = false;

    {
        Promise<bool> promise(true);
        Future<bool> future = promise.GetFuture();
        ASSERT_TRUE(future.IsOK());
        future.OnAbandoned(std::bind(&OnAbandoned, ::_1, &checkAbandoned))
            .OnComplete(std::bind(&OnComplete, ::_1, &checkComplete));
    }

    EXPECT_FALSE(checkAbandoned);
    EXPECT_TRUE(checkComplete);
}

TEST(FutureTest, AbandonCompleteInitFuture)
{
    bool checkAbandoned = false;
    bool checkComplete = false;

    {
        Promise<bool> promise;
        Future<bool> future = promise.GetFuture();
        ASSERT_TRUE(future.IsInit());
        future.OnAbandoned(std::bind(&OnAbandoned, ::_1, &checkAbandoned))
            .OnComplete(std::bind(&OnCompleteInit, ::_1, &checkComplete));
    }

    EXPECT_TRUE(checkAbandoned);
    EXPECT_FALSE(checkComplete);
}

TEST(FutureTest, Associate)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Promise<bool> promise_;
    Future<bool> future_ = promise_.GetFuture();
    ASSERT_TRUE(future_.IsInit());

    promise.Associate(future_);
    promise_.SetValue(true);
    promise_.SetValue(false);

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
}

TEST(FutureTest, AssociateError)
{
    const int32_t code = ERROR_CODE;
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Promise<bool> promise_;
    Future<bool> future_ = promise_.GetFuture();
    ASSERT_TRUE(future_.IsInit());

    promise.Associate(future_);
    promise_.SetFailed(code);
    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(code, future.GetErrorCode());
}

TEST(FutureTest, OnComplete)
{
    bool check1 = false;
    bool check2 = false;
    bool check3 = false;

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    future.OnComplete(std::bind(&OnComplete, ::_1, &check1))
        .OnComplete(std::bind(&OnComplete, ::_1, &check2))
        .OnComplete(std::bind(&OnComplete, ::_1, &check3));

    EXPECT_TRUE(check1);
    EXPECT_TRUE(check2);
    EXPECT_TRUE(check3);
}

TEST(FutureTest, OnCompleteError)
{
    int32_t check1 = 0;
    int32_t check2 = 0;
    int32_t check3 = 0;

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    future.OnComplete(std::bind(&OnCompleteError, ::_1, &check1))
        .OnComplete(std::bind(&OnCompleteError, ::_1, &check2))
        .OnComplete(std::bind(&OnCompleteError, ::_1, &check3));

    EXPECT_EQ(check1, ERROR_CODE);
    EXPECT_EQ(check2, ERROR_CODE);
    EXPECT_EQ(check3, ERROR_CODE);
}

TEST(FutureTest, OnAbandoned)
{
    bool check1 = false;
    bool check2 = false;
    bool check3 = false;

    {
        Promise<bool> promise;
        Future<bool> future = promise.GetFuture();
        ASSERT_TRUE(future.IsInit());

        future.OnAbandoned(std::bind(&OnAbandoned, ::_1, &check1))
            .OnAbandoned(std::bind(&OnAbandoned, ::_1, &check2))
            .OnAbandoned(std::bind(&OnAbandoned, ::_1, &check3));
    }

    EXPECT_TRUE(check1);
    EXPECT_TRUE(check2);
    EXPECT_TRUE(check3);
}

TEST(FutureTest, Then)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(&value);
    ASSERT_TRUE(promise.GetFuture().IsOK());

    future = promise.GetFuture().Then(std::bind(&itoa_string, ::_1));

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("42", future.Get());
}

TEST(FutureTest, ThenError)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(value);
    ASSERT_TRUE(promise.GetFuture().IsError());

    future = promise.GetFuture().Then(std::bind(&itoa_string, ::_1));

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(value, future.GetErrorCode());
}

TEST(FutureTest, ThenNone)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(&value);
    ASSERT_TRUE(promise.GetFuture().IsOK());

    future = promise.GetFuture().Then(&tostring);

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("42", future.Get());
}

TEST(FutureTest, ThenBindNone)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(&value);
    ASSERT_TRUE(promise.GetFuture().IsOK());

    future = promise.GetFuture().Then(std::bind(tostring));

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("42", future.Get());
}

TEST(FutureTest, ThenNoneError)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(value);
    ASSERT_TRUE(promise.GetFuture().IsError());

    future = promise.GetFuture().Then(&tostring);

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(value, future.GetErrorCode());
}

TEST(FutureTest, ThenBindNoneError)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(value);
    ASSERT_TRUE(promise.GetFuture().IsError());

    future = promise.GetFuture().Then(std::bind(tostring));

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(value, future.GetErrorCode());
}

TEST(FutureTest, ThenFuture)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(&value);
    ASSERT_TRUE(promise.GetFuture().IsOK());

    future = promise.GetFuture().Then(std::bind(&itoa_future, ::_1));

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("42", future.Get());
}

TEST(FutureTest, ThenFutureError)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(value);
    ASSERT_TRUE(promise.GetFuture().IsError());

    future = promise.GetFuture().Then(std::bind(&itoa_future, ::_1));

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(value, future.GetErrorCode());
}

TEST(FutureTest, ThenFutureNone)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(&value);
    ASSERT_TRUE(promise.GetFuture().IsOK());

    future = promise.GetFuture().Then(&tofuture);

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("42", future.Get());
}

TEST(FutureTest, ThenFutureBindNone)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(&value);
    ASSERT_TRUE(promise.GetFuture().IsOK());

    future = promise.GetFuture().Then(std::bind(tofuture));

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("42", future.Get());
}

TEST(FutureTest, ThenFutureNoneError)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(value);
    ASSERT_TRUE(promise.GetFuture().IsError());

    future = promise.GetFuture().Then(&tofuture);

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(value, future.GetErrorCode());
}

TEST(FutureTest, ThenFutureBindNoneError)
{
    int value = 42;
    Promise<int *> promise;
    Future<std::string> future;
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(value);
    ASSERT_TRUE(promise.GetFuture().IsError());

    future = promise.GetFuture().Then(std::bind(tofuture));

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(value, future.GetErrorCode());
}

TEST(FutureTest, Chain)
{
    Future<std::string> future =
        readyFuture().Then(&first).Then(std::bind(&second, ::_1)).Then(std::bind(&third, ::_1));

    future.Wait();
    future.WaitFor(100 /*ms*/);

    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("(true)", future.Get());
}

TEST(FutureTest, ChainError)
{
    Future<std::string> future =
        failedFuture().Then(&first).Then(std::bind(&second, ::_1)).Then(std::bind(&third, ::_1));

    future.Wait();
    future.WaitFor(100 /*ms*/);

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
}

TEST(FutureTest, ChainWait)
{
    Promise<bool> promise;
    Future<std::string> future =
        pendingFuture(promise.GetFuture()).Then(&first).Then(std::bind(&second, ::_1)).Then(std::bind(&third, ::_1));

    ASSERT_TRUE(future.IsInit());

    future.WaitFor(100 /*ms*/);
    ASSERT_TRUE(future.IsInit());

    promise.SetValue("true");
    ASSERT_TRUE(future.IsOK());
    EXPECT_EQ("(true)", future.Get());
}

TEST(FutureTest, ChainWaitError)
{
    Promise<bool> promise;
    Future<std::string> future =
        pendingFuture(promise.GetFuture()).Then(&first).Then(std::bind(&second, ::_1)).Then(std::bind(&third, ::_1));

    ASSERT_TRUE(future.IsInit());

    future.WaitFor(100 /*ms*/);
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_AbandonComplete)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    future.OnComplete(Defer(aid, &FutureActor::func14, ::_1));
    EXPECT_TRUE(future.IsInit());
}

TEST_F(FutureDeferTest, THREADSAFE_Abandon)
{
    bool checkAbandoned = false;
    bool checkComplete = false;

    {
        Promise<bool> promise;
        Future<bool> future = promise.GetFuture();
        ASSERT_TRUE(future.IsInit());

        future.OnComplete(Defer(aid, &FutureActor::func15, ::_1, &checkComplete))
            .OnAbandoned(Defer(aid, &FutureActor::func15, ::_1, &checkAbandoned));
        EXPECT_TRUE(future.IsInit());
    }

    Future<bool> f;
    f.WaitFor(100 /* ms */);

    EXPECT_TRUE(checkAbandoned);
    EXPECT_FALSE(checkComplete);
}

TEST_F(FutureDeferTest, THREADSAFE_CompleteAbandon)
{
    bool checkAbandoned = false;
    bool checkComplete = false;

    {
        Promise<bool> promise(true);
        Future<bool> future = promise.GetFuture();
        ASSERT_TRUE(future.IsOK());

        future.OnComplete(Defer(aid, &FutureActor::func15, ::_1, &checkComplete))
            .OnAbandoned(Defer(aid, &FutureActor::func15, ::_1, &checkAbandoned));
        EXPECT_TRUE(future.IsOK());
    }

    Future<bool> f;
    f.WaitFor(100 /* ms */);

    EXPECT_FALSE(checkAbandoned);
    EXPECT_TRUE(checkComplete);
}

TEST_F(FutureDeferTest, THREADSAFE_Wait)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Async(aid, &FutureActor::func12, promise);
    future.Wait();

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_WaitError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Async(aid, &FutureActor::func13, promise);
    future.Wait();

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_WaitFor)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Async(aid, &FutureActor::func12, promise);
    ASSERT_TRUE(future.WaitFor(100 /*ms*/).IsOK());

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_WaitForError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Async(aid, &FutureActor::func13, promise);
    ASSERT_TRUE(future.WaitFor(100 /*ms*/).IsOK());

    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_GetWaitFor)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Async(aid, &FutureActor::func12, promise);

    Option<bool> option = future.Get(100 /*ms*/);
    option = future.Get(100 /*ms*/);
    ASSERT_TRUE(option.IsSome());
    EXPECT_TRUE(option.Get());

    ASSERT_TRUE(future.IsOK());
    EXPECT_TRUE(future.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_GetWaitForError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Async(aid, &FutureActor::func13, promise);

    Option<bool> option = future.Get(100 /*ms*/);
    option = future.Get(100 /*ms*/);
    ASSERT_TRUE(option.IsNone());
    ASSERT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());

    option = future.Get(100 /*ms*/);
    ASSERT_TRUE(option.IsNone());
}

TEST_F(FutureDeferTest, THREADSAFE_GetWaitForErrorError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    Option<bool> option = future.Get(100 /*ms*/);
    option = future.Get(100 /*ms*/);
    ASSERT_TRUE(option.IsNone());
    ASSERT_TRUE(future.IsInit());

    option = future.Get(100 /*ms*/);
    ASSERT_TRUE(option.IsNone());
}

TEST_F(FutureDeferTest, THREADSAFE_OnCompleteDefer)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01(_));

    EXPECT_CALL(*actor, func02(_, _));

    EXPECT_CALL(*actor, func03(_, _, _));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    Future<bool> f = future.OnComplete(Defer(aid, &FutureActor::func00))
                         .OnComplete(Defer(aid, &FutureActor::func01, future))
                         .OnComplete(Defer(aid, &FutureActor::func02, future, true))
                         .OnComplete(Defer(aid, &FutureActor::func03, future, true, 0));

    EXPECT_TRUE(future.IsOK());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsOK());
    EXPECT_TRUE(f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_OnCompleteDeferError)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01(_));

    EXPECT_CALL(*actor, func02(_, _));

    EXPECT_CALL(*actor, func03(_, _, _));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    future.OnComplete(Defer(aid, &FutureActor::func00))
        .OnComplete(Defer(aid, &FutureActor::func01, future))
        .OnComplete(Defer(aid, &FutureActor::func02, future, true))
        .OnComplete(Defer(aid, &FutureActor::func03, future, true, 0));

    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_OnCompleteDeferLambda)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01(_));

    EXPECT_CALL(*actor, func02(_, _));

    EXPECT_CALL(*actor, func03(_, _, _));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    Future<bool> f = future.OnComplete(Defer(aid, &FutureActor::func00))
                         .OnComplete(Defer(aid, &FutureActor::func01, ::_1))
                         .OnComplete(Defer(aid, &FutureActor::func02, ::_1, true))
                         .OnComplete(Defer(aid, &FutureActor::func03, ::_1, true, 0));

    EXPECT_TRUE(future.IsOK());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsOK());
    EXPECT_TRUE(f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_OnCompleteDeferLambdaError)
{
    EXPECT_CALL(*actor, func00());

    EXPECT_CALL(*actor, func01(_));

    EXPECT_CALL(*actor, func02(_, _));

    EXPECT_CALL(*actor, func03(_, _, _));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    Future<bool> f = future.OnComplete(Defer(aid, &FutureActor::func00))
                         .OnComplete(Defer(aid, &FutureActor::func01, ::_1))
                         .OnComplete(Defer(aid, &FutureActor::func02, ::_1, true))
                         .OnComplete(Defer(aid, &FutureActor::func03, ::_1, true, 0));

    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsError());
    EXPECT_EQ(ERROR_CODE, f.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDefer)
{
    EXPECT_CALL(*actor, func04()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func05(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func06(_, _)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func07(_, _, _)).WillOnce(Return(true));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    Future<bool> f = future.Then(Defer(aid, &FutureActor::func04))
                         .Then(Defer(aid, &FutureActor::func05, ::_1))
                         .Then(Defer(aid, &FutureActor::func06, ::_1, true))
                         .Then(Defer(aid, &FutureActor::func07, ::_1, true, 0));

    EXPECT_TRUE(future.IsOK());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsOK());
    EXPECT_TRUE(f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferFuture)
{
    EXPECT_CALL(*actor, func08()).WillOnce(Return(true));

    EXPECT_CALL(*actor, func09(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func10(_, _)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func11(_, _, _)).WillOnce(Return(true));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    Future<bool> f = future.Then(Defer(aid, &FutureActor::func08))
                         .Then(Defer(aid, &FutureActor::func09, ::_1))
                         .Then(Defer(aid, &FutureActor::func10, ::_1, true))
                         .Then(Defer(aid, &FutureActor::func11, ::_1, true, 0));

    EXPECT_TRUE(future.IsOK());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsOK());
    EXPECT_TRUE(f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferFunction)
{
    EXPECT_CALL(*actor, func04()).WillRepeatedly(Return(true));

    EXPECT_CALL(*actor, func05(_)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func06(_, _)).WillOnce(Return(true));

    EXPECT_CALL(*actor, func07(_, _, _)).WillOnce(Return(true));

    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    std::function<bool()> func03([]() { return true; });
    std::function<Future<bool>()> func04 = Defer(aid, &FutureActor::func04);
    std::function<Future<bool>(const Future<bool> &)> func05 = Defer(aid, &FutureActor::func05, ::_1);
    std::function<Future<bool>(const Future<bool> &)> func06 = Defer(aid, &FutureActor::func06, ::_1, true);
    std::function<Future<bool>(const Future<bool> &)> func07 = Defer(aid, &FutureActor::func07, ::_1, true, 0);

    promise.SetValue(true);
    Future<bool> f = future.Then(func03)
                         .Then(func04)
                         .Then(std::bind(func04))
                         .Then(std::bind(func05, ::_1))
                         .Then(std::bind(func06, ::_1))
                         .Then(std::bind(func07, ::_1));

    EXPECT_TRUE(future.IsOK());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsOK());
    EXPECT_TRUE(f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferStaticFunction)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetValue(true);
    Future<bool> f = future.Then(&func00)
                         .Then(Defer(aid, std::bind(func00)))
                         .Then(Defer(aid, std::bind(func01, ::_1)))
                         .Then(Defer(aid, std::bind(func02, ::_1, true)))
                         .Then(Defer(aid, std::bind(func03, ::_1, true, 0)));

    EXPECT_TRUE(future.IsOK());
    EXPECT_TRUE(f.WaitFor(100 /*ms*/).IsOK());
    EXPECT_TRUE(f.IsOK());
    EXPECT_TRUE(f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());

    Future<bool> f = future.Then(Defer(aid, &FutureActor::func04))
                         .Then(Defer(aid, &FutureActor::func05, ::_1))
                         .Then(Defer(aid, &FutureActor::func06, ::_1, true))
                         .Then(Defer(aid, &FutureActor::func07, ::_1, true, 0));

    EXPECT_EQ(ERROR_CODE, f.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferFutureError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());

    Future<bool> f = future.Then(Defer(aid, &FutureActor::func08))
                         .Then(Defer(aid, &FutureActor::func09, ::_1))
                         .Then(Defer(aid, &FutureActor::func10, ::_1, true))
                         .Then(Defer(aid, &FutureActor::func11, ::_1, true, 0));

    EXPECT_EQ(ERROR_CODE, f.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferFunctionError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());

    std::function<bool()> func03([]() { return true; });
    std::function<Future<bool>()> func04 = Defer(aid, &FutureActor::func04);
    std::function<Future<bool>(const Future<bool> &)> func05 = Defer(aid, &FutureActor::func05, ::_1);
    std::function<Future<bool>(const Future<bool> &)> func06 = Defer(aid, &FutureActor::func06, ::_1, true);
    std::function<Future<bool>(const Future<bool> &)> func07 = Defer(aid, &FutureActor::func07, ::_1, true, 0);

    Future<bool> f = future.Then(func03)
                         .Then(func04)
                         .Then(std::bind(func04))
                         .Then(std::bind(func05, ::_1))
                         .Then(std::bind(func06, ::_1))
                         .Then(std::bind(func07, ::_1));

    EXPECT_EQ(ERROR_CODE, f.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_ThenDeferStaticFunctionError)
{
    Promise<bool> promise;
    Future<bool> future = promise.GetFuture();
    ASSERT_TRUE(future.IsInit());

    promise.SetFailed(ERROR_CODE);
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());

    Future<bool> f = future.Then(&func00)
                         .Then(Defer(aid, std::bind(func00)))
                         .Then(Defer(aid, std::bind(func01, ::_1)))
                         .Then(Defer(aid, std::bind(func02, ::_1, true)))
                         .Then(Defer(aid, std::bind(func03, ::_1, true, 0)));

    EXPECT_EQ(ERROR_CODE, f.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_After)
{
    std::atomic_bool executed(false);

    Promise<int> promise;
    promise.SetValue(0);
    Future<int> future = promise.GetFuture();
    Future<int> f = future.After(100 /*ms*/, std::bind(&after, &executed, ::_1));

    Promise<int> promise_;
    Future<int> future_ = promise_.GetFuture();
    EXPECT_TRUE(future_.WaitFor(200 /*ms*/).IsError());

    EXPECT_TRUE(future.IsOK());
    EXPECT_FALSE(executed.load());

    EXPECT_TRUE(f.IsOK());
    EXPECT_EQ(0, f.Get());
}

TEST_F(FutureDeferTest, THREADSAFE_AfterError)
{
    std::atomic_bool executed(false);

    Promise<int> promise;
    promise.SetFailed(ERROR_CODE);
    Future<int> future = promise.GetFuture();
    Future<int> f = future.After(100 /*ms*/, std::bind(&after, &executed, ::_1));

    Promise<int> promise_;
    Future<int> future_ = promise_.GetFuture();
    EXPECT_TRUE(future_.WaitFor(200 /*ms*/).IsError());

    EXPECT_TRUE(future.IsError());
    EXPECT_FALSE(executed.load());

    EXPECT_TRUE(f.IsError());
    EXPECT_EQ(ERROR_CODE, f.GetErrorCode());
}

TEST_F(FutureDeferTest, THREADSAFE_AfterTimeOut)
{
    std::atomic_bool executed(false);

    Promise<int> promise;
    Future<int> future = promise.GetFuture();
    Future<int> f = future.After(100 /*ms*/, std::bind(&after, &executed, ::_1));

    EXPECT_TRUE(future.WaitFor(500 /*ms*/).IsError());

    EXPECT_TRUE(future.IsInit());
    EXPECT_TRUE(executed.load());

    EXPECT_TRUE(f.IsInit());
}

TEST_F(FutureDeferTest, THREADSAFE_SetValueAfterTimeOut)
{
    std::atomic_bool executed(false);

    Promise<int> promise;
    Future<int> future = promise.GetFuture();
    Future<int> f = future.After(100 /*ms*/, std::bind(&after, &executed, ::_1));
    future.SetValue(0);

    EXPECT_TRUE(future.WaitFor(500 /*ms*/).IsOK());

    EXPECT_TRUE(future.IsOK());
    EXPECT_EQ(future.Get(), 0);
    EXPECT_FALSE(executed.load());

    EXPECT_TRUE(f.IsOK());
}

TEST_F(FutureDeferTest, THREADSAFE_SetFailedAfterTimeOut)
{
    std::atomic_bool executed(false);

    Promise<int> promise;
    Future<int> future = promise.GetFuture();
    Future<int> f = future.After(100 /*ms*/, std::bind(&after, &executed, ::_1));
    future.SetFailed(ERROR_CODE);

    EXPECT_TRUE(future.WaitFor(500 /*ms*/).IsOK());

    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(ERROR_CODE, future.GetErrorCode());
    EXPECT_FALSE(executed.load());

    EXPECT_TRUE(f.IsError());
}
