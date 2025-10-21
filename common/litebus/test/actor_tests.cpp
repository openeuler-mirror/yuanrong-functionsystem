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

#include <gtest/gtest.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

#include <sstream>
#include <string>

#define private public
#define protected public
#include "actor/actorapp.hpp"
#include "actor/actormgr.hpp"
#include "async/async.hpp"
#include "litebus.h"
#include "litebus.hpp"
#include "timer/timertools.hpp"
#include "utils/os_utils.hpp"

using std::placeholders::_1;

extern std::string g_Protocol;
extern std::string g_localip;
char *testServerPath = (char *)"./server-stability";

namespace litebus {
const std::string ACTOR1("TestActor1");
const char *ACTOR2 = "TestActor2";

class TestActor2;

std::shared_ptr<TestActor2> test_actor2;

// for test5
void callbackTest5(const litebus::Future<std::string> &msg)
{
    EXPECT_EQ("test5", msg.Get());
    return;
}

// for test8
void callbackTest8(const litebus::Future<std::string> &msg)
{
    EXPECT_EQ("test8", msg.Get());
    return;
}

// for test6
void callbackTest6(const litebus::Future<int> &msg)
{
    EXPECT_EQ(6, msg.Get());
    return;
}

class TestMessage : public litebus::MessageBase {
public:
    TestMessage(std::string name) : MessageBase(name)
    {
    }

    TestMessage() : data_("TTTT"), data2_(11)
    {
    }

    ~TestMessage(){};

    std::string Data()
    {
        return "Test Data";
    }

    std::string data_;
    int data2_;
};

class TestActor2 : public litebus::ActorBase {
public:
    TestActor2(std::string name) : ActorBase(name)
    {
    }

    ~TestActor2()
    {
    }

    void test3(const std::string &msg)
    {
        EXPECT_EQ("test3", msg);
    }

    void test4(const std::string &msg)
    {
        EXPECT_EQ("test4", msg);
    }

    litebus::Future<std::string> test5()
    {
        std::shared_ptr<litebus::Promise<std::string>> promise1(new litebus::Promise<std::string>());
        _test5(promise1);
        return promise1->GetFuture();
    }

    void _test5(const std::shared_ptr<litebus::Promise<std::string>> &promise1)
    {
        std::string rpmsg = "test5";
        promise1->SetValue(rpmsg);
    }

    int test6(const int &id, const std::string &msg)
    {
        EXPECT_EQ("test6", msg);
        return id;
    }

    void test7(const int &id, const std::string &msg)
    {
        EXPECT_EQ("test7", msg);
    }

    litebus::Future<std::string> test8(const int &id, const std::string &msg)
    {
        EXPECT_EQ("test8", msg);
        std::shared_ptr<litebus::Promise<std::string>> promise(new litebus::Promise<std::string>());
        promise->SetValue(msg);
        return promise->GetFuture();
    }

    void HandleHttp(std::unique_ptr<MessageBase> message)
    {
        handleHttpRun = true;
    }

    void Exited(const litebus::AID &from)
    {
        exitedRun = true;
    }

    void HandleLocalMsg(std::unique_ptr<MessageBase> msg)
    {
        handleLocalRun = true;
    }

public:
    bool exitedRun = false;
    bool handleHttpRun = false;
    bool handleLocalRun = false;
    bool handleKmsg = false;

private:
    void test2(std::unique_ptr<TestMessage> msg)
    {
        BUSLOG_INFO(msg->From().Name());
        BUSLOG_INFO("name {}, from {}", msg->Name(), msg->From().Name());
        return;
    }

    void test_f(const litebus::AID &from, std::string &&name, std::string &&body)
    {
        handleKmsg = true;
        return;
    }

    virtual void Init() override
    {
        // register receive handle
        Receive("testMsg", &TestActor2::test_f);

        // register receive udp handle
        ReceiveUdp("testMsgUdp", &TestActor2::test_f);

        handleKmsg = false;
        return;
    }
};

class TestActor1 : public litebus::ActorBase {
public:
    TestActor1(std::string name) : ActorBase(name)
    {
    }

    ~TestActor1()
    {
    }

private:
    void test1(std::string from, TestMessage msg)
    {
        BUSLOG_INFO("Receive test message");
        return;
    }

    virtual void Init() override
    {
        BUSLOG_INFO(" send string message");
        std::string strMsg = "test_f";
        Send(ACTOR2, "testMsg", std::move(strMsg));
        Send(ACTOR2, "testMsg", "test_f");

        BUSLOG_INFO("dispatch message : return null");
        std::string data3("test3");
        Async(ACTOR2, &TestActor2::test3, data3);

        std::string data4("test4");
        Async(ACTOR2, &TestActor2::test4, data4);

        BUSLOG_INFO("dispatch message : return F");
        std::string data5("test local send, 5555");
        Async(ACTOR2, &TestActor2::test5).OnComplete(std::bind(callbackTest5, ::_1));

        std::string data6("test6");
        for (int i = 0; i < 3; i++) {
            Async(ACTOR2, &TestActor2::test6, 6, data6).OnComplete(std::bind(callbackTest6, ::_1));
        }

        std::string data7("test7");
        for (int i = 0; i < 3; i++) {
            Async(ACTOR2, &TestActor2::test7, i, data7);
        }

        const std::string data8("test8");
        for (int i = 0; i < 3; i++) {
            Async(ACTOR2, &TestActor2::test8, i, data8).OnComplete(std::bind(callbackTest8, ::_1));
        }
    }
};

class TemplateMessage {
public:
    virtual ~TemplateMessage()
    {
        // BUS_LOG(INFO) << "call ~TemplateMessage";
    }

public:
    std::string name;    // message name
};

class A : public TemplateMessage {
public:
    A()
    {
        data = new int(1);
    };
    ~A()
    {
        delete data;
        // BUS_LOG(INFO) << "call A::~A";
    };

private:
    int *data;
};

class Worker2 : public litebus::ActorBase {
public:
    Worker2(std::string name) : ActorBase(name)
    {
    }

    litebus::Future<std::shared_ptr<TemplateMessage>> HandleTemplateMessage(
        const std::shared_ptr<TemplateMessage> &msg)
    {
        if ("A" == msg->name) {
            BUSLOG_INFO("HandleTemplateMessage get message: {}", msg->name);
        }

        // (1) return a template msg
        std::shared_ptr<TemplateMessage> ret(new A());
        ret->name = "A";
        return ret;
    }
    // void Init() {}
};

class Worker1 : public litebus::ActorBase {
public:
    Worker1(std::string name) : ActorBase(name)
    {
    }

    void Init()
    {
        std::shared_ptr<TemplateMessage> msg(new A());
        msg->name = "A";
        AID to("Worker2", this->GetAID().Url());
        BUSLOG_INFO("Test Link");
        Link(to);
        BUSLOG_INFO("Test Reconnect");
        Reconnect(to);
        BUSLOG_INFO("Test UnLink");
        UnLink(to);
        BUSLOG_INFO("Test Async");
        litebus::Future<std::shared_ptr<TemplateMessage>> ret =
            litebus::Async(to, &Worker2::HandleTemplateMessage, msg);
        BUSLOG_INFO("before future Get");
        EXPECT_EQ("A", ret.Get()->name);
        BUSLOG_INFO("after future Get");
    }
};

class App1 : public litebus::AppActor {
public:
    App1(std::string name) : AppActor(name)
    {
    }

    void f1(const AID &from, std::unique_ptr<TemplateMessage> msg)
    {
        EXPECT_EQ("A", msg->name);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void Init()
    {
        Receive("f1", &App1::f1);
    }
};
class App2 : public litebus::AppActor {
public:
    App2(std::string name) : AppActor(name)
    {
    }

    void Init()
    {
        std::unique_ptr<TemplateMessage> msg(new A());
        msg->name = "A";
        std::string to("app1");
        this->Send(to, "f1", std::move(msg));

        std::unique_ptr<TemplateMessage> msg2(new A());
        msg2->name = "A";
        this->Send("app1", "f2", std::move(msg2));
    }
};
class LongTimeActor : public litebus::ActorBase {
public:
    LongTimeActor(std::string name) : ActorBase(name)
    {
    }

    void LongRun(const AID &from, std::string &&name, std::string &&body)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    void Init()
    {
        Receive("LongRun", &LongTimeActor::LongRun);
    }
};

std::string serUrl = "tcp://127.0.0.1:4100";
std::string serName = "server_";

class ClientActor : public litebus::ActorBase {
public:
    ClientActor(std::string name) : ActorBase(name)
    {
    }

    void SendMsg(AID &_to, std::string &msgname, int msgsize, int msgnum, bool remoteLink)
    {
        std::string data(msgsize, 'A');
        BUSLOG_INFO("begin send msg]num={}, to={}, name={}, size={}, remoteLink={}", msgnum, std::string(_to), msgname,
                    msgsize, remoteLink);
        while (msgnum > 0) {
            this->Send(_to, std::move(msgname), std::move(data), remoteLink);
            uint64_t outBufferSize = this->GetOutBufSize(_to);
            BUSLOG_DEBUG("send msg]msgnum={}, to={}, msgname={}, msgsize={}, remoteLink={}, outBufferSize={}", msgnum,
                         std::string(_to), msgname, msgsize, remoteLink, outBufferSize);
            msgnum--;
        }
        BUSLOG_INFO("end send msg]num={}", msgnum);
    }

    void Init()
    {
    }
};

class ActorTest : public ::testing::Test {
protected:
    std::vector<pid_t> pids;

    pid_t startServerByParams(char *path, char **args)
    {
        pid_t pid = fork();
        if (pid == 0) {
            if (execv(path, args) == -1) {
                BUSLOG_INFO("execve failed, errno: {}, args: {}, args[0]: {}", errno, *args, path);
            }
            return -1;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return pid;
        }
    }

    bool startServer(std::string connType)
    {
        char *args[5];
        args[0] = (char *)serUrl.data();
        serName = "server_" + connType;
        args[1] = (char *)serName.data();
        std::string actorNumStr = "1";
        args[2] = (char *)(actorNumStr.data());
        std::string testType = "flow_control";
        args[3] = (char *)testType.data();
        args[4] = nullptr;
        pid_t pid = startServerByParams(testServerPath, args);
        if (pid > 0) {
            pids.push_back(pid);
            BUSLOG_INFO("start server sucess]pid={}", pid);
        } else {
            BUSLOG_INFO("start server sucess]localurl={}, args: {}", serUrl, *args);
            return false;
        }

        return true;
    }

    void shutdownServer(pid_t pid)
    {
        if (pid > 1) {
            kill(pid, SIGALRM);
            int status;
            waitpid(pid, &status, 0);
            BUSLOG_INFO("status = {}", status);
        }
    }

    void SetUp()
    {    // BUS_LOG(INFO) << "start";
    }

    void TearDown()
    {
        //  BUS_LOG(INFO) << "stop";
        for (unsigned int i = 0; i < pids.size(); i++) {
            shutdownServer(pids[i]);
            BUSLOG_INFO("shutdown server]i={}, pid={}", i, pids[i]);
            pids[i] = 0;
        }
        litebus::TerminateAll();
    }
};

// make sure FinalizeAndInitTimer is first test
TEST_F(ActorTest, FinalizeAndInitTimer)
{
    bool timerStatus = true;
    TimerTools::Finalize();
    TimerTools::Finalize();
    timerStatus = TimerTools::initStatus.load();
    EXPECT_FALSE(timerStatus == true);
    bool ret = TimerTools::Initialize();
    EXPECT_EQ(true, ret);
    timerStatus = TimerTools::initStatus.load();
    EXPECT_EQ(true, timerStatus);
}

TEST_F(ActorTest, bufferSize)
{
    BUSLOG_INFO("get buffer]g_Protocol={}", g_Protocol);
    bool ret = startServer(g_Protocol);
    EXPECT_EQ(true, ret);
    auto client_actor = std::make_shared<ClientActor>("client_0");
    auto clientID = litebus::Spawn(client_actor);
    std::string serActName = serName + "_" + std::to_string(0);
    AID to(serActName, serUrl);
    BUSLOG_INFO("get buffer]to={}", std::string(to));
    std::string msgName = "bufferMessage";
    client_actor->SendMsg(to, msgName, 10 * 1024 * 1024, 50, false);
    ret = false;
    for (int i = 0; i < 50; i++) {
        uint64_t outBufSize = client_actor->GetOutBufSize(to);
        uint64_t inBufSize = client_actor->GetInBufSize(to);
        if ((outBufSize > 0) || ((inBufSize > 0))) {
            ret = true;
            BUSLOG_INFO("get tcp buffer]outBufSize={}, inBufSize={}", outBufSize, inBufSize);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    EXPECT_EQ(true, ret);
}

#ifdef UDP_ENABLED
TEST_F(ActorTest, udpBufferSize)
{
    auto client_actor = std::make_shared<ClientActor>("client_0");
    auto clientID = litebus::Spawn(client_actor);
    std::string serActName = serName + "_" + std::to_string(0);
    serUrl = "udp://127.0.0.1:4000";
    AID to(serActName, serUrl);
    EXPECT_EQ(true, client_actor->GetOutBufSize(to) == 1);
    EXPECT_EQ(true, client_actor->GetInBufSize(to) == 1);
}
#endif

TEST_F(ActorTest, getAID)
{
    auto actor = std::make_shared<TestActor2>(ACTOR2);
    auto aid = litebus::Spawn(actor, false);
    EXPECT_EQ(aid, actor->GetAID());
}

TEST_F(ActorTest, MsgType)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    auto myid = litebus::Spawn(test_actor2);
    std::unique_ptr<MessageBase> msg;

    std::string url = ActorMgr::GetActorMgrRef()->GetUrl(BUS_UDP);
    AID to(ACTOR2, url);

    if (!url.empty()) {
        // KMSG, send tcp message to udp actor
        msg.reset(new MessageBase("testMsg", MessageBase::Type::KMSG));
        ActorMgr::GetActorMgrRef()->Send(to, std::move(msg));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_FALSE(test_actor2->handleKmsg);
        test_actor2->handleKmsg = false;

        // KMSG, send udp message to udp actor
        msg.reset(new MessageBase("testMsgUdp", MessageBase::Type::KMSG));
        ActorMgr::GetActorMgrRef()->Send(to, std::move(msg));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_TRUE(test_actor2->handleKmsg);
        test_actor2->handleKmsg = false;
    }

    // KMSG, send tcp message with tcp message name
    msg.reset(new MessageBase("testMsg", MessageBase::Type::KMSG));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(test_actor2->handleKmsg);
    test_actor2->handleKmsg = false;

    // KMSG, send tcp message with udp message name
    msg.reset(new MessageBase("testMsgUdp", MessageBase::Type::KMSG));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(test_actor2->handleKmsg);
    test_actor2->handleKmsg = false;

    // KMSG, send udp message with tcp message name
    msg.reset(new MessageBase("testMsg", MessageBase::Type::KUDP));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(test_actor2->handleKmsg);
    test_actor2->handleKmsg = false;

    // KMSG, send udp message with udp message name
    msg.reset(new MessageBase("testMsgUdp", MessageBase::Type::KUDP));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(test_actor2->handleKmsg);
    test_actor2->handleKmsg = false;

    // Exit
    msg.reset(new MessageBase(MessageBase::Type::KEXIT));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(true, test_actor2->exitedRun);
    // http
    msg.reset(new MessageBase(MessageBase::Type::KHTTP));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(true, test_actor2->handleHttpRun);
    // KLOCAL
    msg.reset(new MessageBase(MessageBase::Type::KLOCAL));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(true, test_actor2->handleLocalRun);
    // KMSG,Null MsgName;
    msg.reset(new MessageBase("_testbump_", MessageBase::Type::KMSG));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    // KMSG,__BUSY__;
    msg.reset(new MessageBase("__BUSY__", MessageBase::Type::KMSG));
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
    // KMSG,Receive;
    msg.reset(new MessageBase("__BUSY__", MessageBase::Type::KMSG));
    msg->SetTo(ACTOR2);
    ActorMgr::Receive(std::move(msg));

    msg.reset(new MessageBase("__BUSY__", MessageBase::Type::KMSG));
    msg->Run(test_actor2.get());
    EXPECT_EQ(myid, test_actor2->GetAID());
}

TEST_F(ActorTest, ActorSharedThread)
{
    auto app2 = std::make_shared<TestActor2>(ACTOR2);
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2, true);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto app1 = std::make_shared<TestActor1>(ACTOR1);
    ASSERT_TRUE(app1 != nullptr);
    litebus::Spawn(app1, true);

    litebus::Terminate(app1->GetAID());
    litebus::Await(app1->GetAID());
}
TEST_F(ActorTest, ActorSingleThread)
{
    auto app2 = std::make_shared<TestActor2>(ACTOR2);
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2, false);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto app1 = std::make_shared<TestActor1>(ACTOR1);
    ASSERT_TRUE(app1 != nullptr);
    litebus::Spawn(app1, false);

    litebus::Terminate(app1->GetAID());
    litebus::Await(app1->GetAID());
}
TEST_F(ActorTest, ActorTwoModelsThread)
{
    auto app2 = std::make_shared<TestActor2>(ACTOR2);
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2, true);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto app1 = std::make_shared<TestActor1>(ACTOR1);
    ASSERT_TRUE(app1 != nullptr);
    litebus::Spawn(app1, false);

    litebus::Terminate(app1->GetAID());
    litebus::Await(app1->GetAID());
}

TEST_F(ActorTest, TestLink)
{
    auto app2 = std::make_shared<Worker2>("Worker2");
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto app1 = std::make_shared<Worker1>("Worker1");
    ASSERT_TRUE(app1 != nullptr);
    litebus::Spawn(app1);
    litebus::Terminate(app1->GetAID());
    litebus::Await(app1->GetAID());
}

TEST_F(ActorTest, TestReturnValue)
{
    class WorkerSend : public litebus::AppActor {
    public:
        WorkerSend(std::string name) : AppActor(name)
        {
        }

        void Init()
        {
            int result;

            // Send

            // ACTOR_NOT_FIND
            {
                std::unique_ptr<TemplateMessage> msg(new A());
                msg->name = "A";
                std::string to("appNotExist");
                result = this->Send(to, "f1", std::move(msg));
                EXPECT_EQ(result, ACTOR_NOT_FIND);
            }

            // ACTOR_PARAMER_ERR
            {
                std::unique_ptr<MessageBase> msg;
                // KEXIT, err msg type;
                msg.reset(new MessageBase("_testbump_", MessageBase::Type::KEXIT));
                std::string to("app1@127.0.0.1:9999");    // app
                result = this->Send(to, "f1", std::move(msg));
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }

            // IO_NOT_FIND
            {
                std::unique_ptr<MessageBase> msg;
                msg.reset(new MessageBase(MessageBase::Type::KMSG));

                std::string to("IO_NOT_FIND@NOTFUND://127.0.0.1:9999");    // app
                AID actor(to);
                result = Send(actor, std::move(msg));
                EXPECT_EQ(result, IO_NOT_FIND);
            }

            // Right
            for (int i = 0; i < 100; i++) {
                std::unique_ptr<TemplateMessage> msg(new A());
                msg->name = "A";
                std::string to("app1");
                result = this->Send(to, "f1", std::move(msg));
            }
            EXPECT_GE(result, 1);
            BUSLOG_INFO(" send return:", result);

            // link

            // IO_NOT_FIND
            {
                std::string to("app1@XXX://127.0.0.1:9999");    // app
                result = this->Link(to);
                EXPECT_EQ(result, IO_NOT_FIND);
            }
            // ACTOR_PARAMER_ERR
            {
                std::string to = "app1@" + g_Protocol + "://127.0.0.1:kkk";    // app
                result = this->Link(to);
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }
            // ACTOR_PARAMER_ERR
            {
                std::string to = "app1@" + g_Protocol + "://127.kk.0.1:8080";    // app
                result = this->Link(to);
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }
            // ERRORCODE_SUCCESS
            {
                std::string to = "app1@" + g_Protocol + "://127.0.0.1:8080";
                result = this->Link(to);
                EXPECT_EQ(result, ERRORCODE_SUCCESS);
            }

            // unlink
            // IO_NOT_FIND
            {
                std::string to("app1@XXX://127.0.0.1:9999");    // app
                result = this->UnLink(to);
                EXPECT_EQ(result, IO_NOT_FIND);
            }
            // ACTOR_PARAMER_ERR
            {
                std::string to = "app1@" + g_Protocol + "://127.0.0.1:kk";    // app
                result = this->UnLink(to);
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }
            // ACTOR_PARAMER_ERR
            {
                std::string to = "app1@" + g_Protocol + "://127.kk.0.1:8080";    // app
                result = this->UnLink(to);
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }
            // ERRORCODE_SUCCESS
            {
                std::string to = "app1@" + g_Protocol + "://127.0.0.1:8080";
                result = this->UnLink(to);
                EXPECT_EQ(result, ERRORCODE_SUCCESS);
            }

            // Reconnect
            // IO_NOT_FIND
            {
                std::string to("app1@XXX://127.0.0.1:9999");    // app
                result = this->Reconnect(to);
                EXPECT_EQ(result, IO_NOT_FIND);
            }
            // ACTOR_PARAMER_ERR
            {
                std::string to = "app1@" + g_Protocol + "://127.0.0.1:kk";    // app
                result = this->Reconnect(to);
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }
            // ACTOR_PARAMER_ERR
            {
                std::string to = "app1@" + g_Protocol + "://127.kk.0.1:8080";    // app
                result = this->Reconnect(to);
                EXPECT_EQ(result, ACTOR_PARAMER_ERR);
            }
            // ERRORCODE_SUCCESS
            {
                std::string to = "app1@" + g_Protocol + "://127.0.0.1:8080";
                result = this->Reconnect(to);
                EXPECT_EQ(result, ERRORCODE_SUCCESS);
            }
        }
    };

    auto app1 = std::make_shared<App1>("app1");
    litebus::Spawn(app1, false);
    auto app2 = std::make_shared<WorkerSend>("app2");
    litebus::Spawn(app2, false);

    litebus::Terminate(app2->GetAID());
    litebus::Await(app2->GetAID());
}

TEST_F(ActorTest, DoubleTestActor_)
{
    auto app2 = std::make_shared<LongTimeActor>(ACTOR2);
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2);
    std::unique_ptr<MessageBase> msg;
    msg.reset(new MessageBase(MessageBase::Type::KMSG));
    msg->SetName("LongRun");
    ActorMgr::GetActorMgrRef()->Send(ACTOR2, std::move(msg));
}

TEST_F(ActorTest, WorkActor_)
{
    auto app2 = std::make_shared<Worker2>("Worker2");
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto app1 = std::make_shared<Worker1>("Worker1");
    ASSERT_TRUE(app1 != nullptr);
    litebus::Spawn(app1);
    litebus::Terminate(app1->GetAID());
    litebus::Await(app1->GetAID());
}

TEST_F(ActorTest, AppActor_)
{
    auto app1 = std::make_shared<App1>("app1");
    ASSERT_TRUE(app1 != nullptr);
    auto myid = litebus::Spawn(app1);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto app2 = std::make_shared<App2>("app2");
    ASSERT_TRUE(app2 != nullptr);
    litebus::Spawn(app2);
    litebus::Terminate(app2->GetAID());
    litebus::Await(app2->GetAID());
}

TEST_F(ActorTest, GetIOMgr)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    auto myid = litebus::Spawn(test_actor2);
    auto io1 = ActorMgr::GetIOMgrRef(myid);
    auto io2 = ActorMgr::GetIOMgrRef(myid.GetProtocol());
    EXPECT_EQ(io1, io2);
}

TEST_F(ActorTest, GetProtocol)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    auto myid = litebus::Spawn(test_actor2);

    EXPECT_EQ(g_Protocol, myid.GetProtocol());
}

TEST_F(ActorTest, GetProtocol2)
{
    AID testhttp("testname", "127.0.0.1:2221");
    EXPECT_EQ("tcp", testhttp.GetProtocol());
    BUSLOG_INFO("{}, protocol: {}", testhttp.GetProtocol());
}

TEST_F(ActorTest, SetProtocol)
{
    AID testhttp("testname", "127.0.0.1:2221");
    testhttp.SetProtocol(BUS_TCP);
    EXPECT_EQ("tcp", testhttp.GetProtocol());
    BUSLOG_INFO("{}, protocol: {}", testhttp.GetProtocol());
}

#ifdef UDP_ENABLED
TEST_F(ActorTest, SetProtocol2)
{
    AID testhttp("testname", "tcp://127.0.0.1:2221");
    testhttp.SetProtocol(BUS_UDP);
    EXPECT_EQ("udp", testhttp.GetProtocol());
    BUSLOG_INFO("{}, protocol: {}", testhttp.GetProtocol());
}
#endif

TEST_F(ActorTest, GetIp)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    auto myid = litebus::Spawn(test_actor2);
    EXPECT_EQ(g_localip, myid.GetIp());
    BUSLOG_INFO("{}, ip: {}", std::string(myid), myid.GetIp());
}

TEST_F(ActorTest, GetIp2)
{
    AID testhttp("testname", "127.0.0.1:2221");
    EXPECT_EQ("127.0.0.1", testhttp.GetIp());
    BUSLOG_INFO("{}, ip: {}", std::string(testhttp), testhttp.GetIp());
}

TEST_F(ActorTest, GetPort)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    auto myid = litebus::Spawn(test_actor2);
    Option<std::string> sPort = os::GetEnv("LITEBUS_PORT");
    EXPECT_EQ(sPort.Get(), std::to_string(myid.GetPort()));
    BUSLOG_INFO("{}, ip: {}", std::string(myid), myid.GetPort());
}

TEST_F(ActorTest, GetPort2)
{
    AID testhttp("testname", "127.0.0.1:2221");
    EXPECT_EQ(2221, testhttp.GetPort());
    BUSLOG_INFO("{}, ip: {}", std::string(testhttp), testhttp.GetPort());
}

TEST_F(ActorTest, string2AID)
{
    std::string str = "actor1@tcp://127.0.0.3:50";
    AID testhttp(str);
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("127.0.0.3:50", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, AIDEq)
{
    AID a1("actor1@tcp://127.0.0.3:50");
    AID a2("actor1@127.0.0.3:50");

    EXPECT_TRUE(a1 == a2);
}
TEST_F(ActorTest, AIDEq1)
{
    AID a1("actor1@tcp://127.0.0.3:50");
    AID a2("actor2@127.0.0.3:50");

    EXPECT_TRUE(a1 < a2);
}
TEST_F(ActorTest, AIDEq2)
{
    AID a1("actor1@tcp://127.0.0.3:50");
    AID a2("actor2@127.0.0.3:50");

    EXPECT_TRUE(a2 > a1);
}

TEST_F(ActorTest, AIDEq3)
{
    AID a1("actor1@tcp://127.0.0.3:50");
    AID a2("actor2@127.0.0.3:50");

    EXPECT_TRUE(a2 != a1);
}

TEST_F(ActorTest, AIDEq4)
{
    AID a1("actor1@tcp://127.0.0.3:50");
    AID a2("actor1@127.0.0.3:50");

    AID a3("actor2@127.0.0.3:50");
    AID a4("actor2@tcp://127.0.0.3:50");

    std::map<AID, int> m;
    m[a1] = 1;
    m[a2] = 1;
    m[a3] = 1;
    m[a4] = 1;

    EXPECT_EQ(int(m.size()), 2);
}

TEST_F(ActorTest, AIDCout)
{
    std::string str = "actor1@tcp://127.0.0.3:50";
    AID testhttp(str);
    std::ostringstream ostr;
    ostr << testhttp;
    str = "actor1@127.0.0.3:50";
    EXPECT_EQ(str, ostr.str());
}
TEST_F(ActorTest, AIDEqui)
{
    std::string str = "actor1@tcp://127.0.0.3:50";
    AID id1(str);
    AID id2("actor1@tcp://127.0.0.3:50");
    EXPECT_EQ(id1, id2);
    AID id3("test");
    AID id4;
    id4.SetName("test");
    EXPECT_EQ(id3, id4);
}

TEST_F(ActorTest, char2AID)
{
    // std::string str=;
    AID testhttp("actor1@tcp://127.0.0.3:50");
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("127.0.0.3:50", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, Twochar2AID)
{
    // std::string str=;
    AID testhttp("actor1", "tcp://127.0.0.3:50");
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("127.0.0.3:50", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, onechar2AID)
{
    // std::string str=;
    AID testhttp("actor1");
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, Twochar2AID2string2AID)
{
    AID testhttp1("actor1", "tcp://127.0.0.3:50");
    std::string a = testhttp1;
    AID testhttp = a;
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("127.0.0.3:50", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, char2AID2string2AID)
{
    AID testhttp1("actor1");
    std::string a = testhttp1;
    AID testhttp = a;
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, AID__char)
{
    AID testhttp1 = "actor1";
    std::string a = testhttp1;
    AID testhttp = a;
    EXPECT_EQ("actor1", testhttp.Name());
    EXPECT_EQ("", testhttp.Url());
    BUSLOG_INFO("{}, name: {}, url: {}", std::string(testhttp), testhttp.Name(), testhttp.Url());
}

TEST_F(ActorTest, getNullActor)
{
    auto testptr = ActorMgr::GetActorMgrRef()->GetActor("nullActor");
    EXPECT_EQ(nullptr, testptr);
}

TEST_F(ActorTest, getNullIOMgr)
{
    std::string protocol = "null";
    auto testptr = ActorMgr::GetIOMgrRef(protocol);
    EXPECT_EQ(nullptr, testptr);
}

TEST_F(ActorTest, getNullUrl)
{
    std::string protocol = "null";
    auto testptr = ActorMgr::GetActorMgrRef()->GetUrl(protocol);

    Option<std::string> sPort = os::GetEnv("LITEBUS_PORT");
    std::string url = g_localip + ":" + sPort.Get();
    EXPECT_EQ(url, testptr);
}

void f1()
{
    BUSLOG_INFO("------------litebus is exiting -------------------------");
}

TEST_F(ActorTest, litebus_init)
{
    AID aid1("test@tcp://fake:35001");
    AID aid2("test@udp://fake:35001");
    EXPECT_FALSE(aid1.OK());
    EXPECT_FALSE(aid2.OK());
}

TEST_F(ActorTest, litebus_MultiInit)
{
    litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");

    EXPECT_TRUE(result == BUS_OK);
}

TEST_F(ActorTest, AddRuleUdpTest)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    auto myid = litebus::Spawn(test_actor2);

    auto ret = test_actor2->AddRuleUdp("123", 1);

    EXPECT_EQ(ret, 0);
}

TEST_F(ActorTest, DelRuleUdpTest)
{
    auto test_actor2 = std::make_shared<TestActor2>(ACTOR2);
    EXPECT_TRUE(test_actor2 != nullptr);
    auto myid = litebus::Spawn(test_actor2);

    test_actor2->DelRuleUdp("123", 1);
}

TEST_F(ActorTest, litebus_InitializeC01)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    LitebusConfig *config = new LitebusConfig();
    int ret = LitebusInitializeC(config);

    EXPECT_TRUE(result == BUS_OK);
    EXPECT_TRUE(ret == -1);
    BUSLOG_INFO(ret);
}
TEST_F(ActorTest, litebus_InitializeC02)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    LitebusConfig *config = new LitebusConfig();
    config->threadCount = 0;
    int ret = LitebusInitializeC(config);

    EXPECT_TRUE(result == BUS_OK);
    EXPECT_TRUE(ret == -1);
}

TEST_F(ActorTest, litebus_InitializeC03)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    LitebusConfig *config = new LitebusConfig();
    config->httpKmsgFlag = 2;
    int ret = LitebusInitializeC(config);

    EXPECT_TRUE(result == BUS_OK);
    EXPECT_TRUE(ret == -1);
}

TEST_F(ActorTest, litebus_InitializeC04)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    LitebusConfig *config = new LitebusConfig();
    config->threadCount = 1;
    config->httpKmsgFlag = 1;
    int ret = LitebusInitializeC(config);

    EXPECT_TRUE(result == BUS_OK);
    EXPECT_TRUE(ret == BUS_OK);

    litebus::SetHttpKmsgFlag(-1);
}

TEST_F(ActorTest, litebus_InitializeC05)
{
    int ret = LitebusInitializeC(nullptr);
    EXPECT_TRUE(ret == -1);
}

TEST_F(ActorTest, litebus_InitializeC06)
{
    LitebusConfig *config = new LitebusConfig();
    config->threadCount = 1;
    config->httpKmsgFlag = 2;
    int ret = LitebusInitializeC(config);
    delete config;
    config = nullptr;

    EXPECT_TRUE(ret == -1);
}

TEST_F(ActorTest, litebus_SetActorStatus)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    AID aid1("test@tcp://fake:35001");
    EXPECT_FALSE(aid1.OK());
    SetActorStatus(aid1, false);
    EXPECT_TRUE(result == BUS_OK);
}
TEST_F(ActorTest, litebus_Await)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    litebus::ActorReference act1(new ActorBase("ActorBase"));
    Await(act1);
    EXPECT_TRUE(result == BUS_OK);
}

TEST_F(ActorTest, litebus_GetActor)
{
    int result = litebus::Initialize("tcp://127.0.0.1:35001", "", "udp://127.0.0.1:35001");
    AID aid1("test@tcp://fake:35001");
    EXPECT_FALSE(aid1.OK());
    GetActor(aid1);
    EXPECT_TRUE(result == BUS_OK);
}
}    // namespace litebus
