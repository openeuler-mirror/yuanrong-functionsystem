#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/option.hpp"
#include "litebus.hpp"
#include "timer/duration.hpp"

using std::placeholders::_1;

const std::string ACTOR1("TestActor1");
const std::string ACTOR2("TestActor2");

class TestActor2;

void callbackTest(const litebus::Future<std::string> &msg)
{
    auto result = msg.Get(5);
    BUSLOG_INFO("Receive1 reply message: msg = {}", result.IsSome() ? result.Get() : "");
    return;
}

void callbackTest2(const litebus::Future<int> &msg)
{
    auto result = msg.Get(5);
    BUSLOG_INFO("Receive2 reply message: id = {}", result.IsSome() ? result.Get() : 0);
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
        BUSLOG_INFO("test 3, string data: {}", msg);
    }

    void test4(const std::string &msg)
    {
        BUSLOG_INFO("test 4, string data: {}", msg);
    }

    litebus::Future<std::string> test5()
    {
        BUSLOG_INFO("test 5, string data arrive");
        std::shared_ptr<litebus::Promise<std::string>> promise1(new litebus::Promise<std::string>());
        _test5(promise1);
        return promise1->GetFuture();
    }

    void _test5(const std::shared_ptr<litebus::Promise<std::string>> &promise1)
    {
        std::string rpmsg = "test local message reply";
        promise1->SetValue(rpmsg);
    }

    int test6(const int &id, const std::string &msg)
    {
        BUSLOG_INFO("test 6, id = {}, string data: {}", id, msg);
        return id;
    }

    void test7(const int &id, const std::string &msg)
    {
        BUSLOG_INFO("test 7, id = {}, string data: {}", id, msg);
    }

    litebus::Future<std::string> test8(const int &id, const std::string &msg)
    {
        BUSLOG_INFO("test 8, id = {}, string data: {}", id, msg);
        std::shared_ptr<litebus::Promise<std::string>> promise(new litebus::Promise<std::string>());
        _test5(promise);
        return promise->GetFuture();
    }

    void testTimer1()
    {
        BUSLOG_INFO("testTimer1");
    }
    void testAsync()
    {
        AsyncAfter(20, GetAID(), &TestActor2::testAsync);
    }

private:
    void test2(std::unique_ptr<TestMessage> msg)
    {
        BUSLOG_INFO(msg->From().Name());
        BUSLOG_INFO("name {}, from {}", msg->Name(), msg->From().Name());
        return;
    }

    void test_f(litebus::AID from, std::string &&name, std::string &&body)
    {
        BUSLOG_INFO("Test From: {}, name: {}, body: {}", std::string(from), name, body);
        return;
    }

    virtual void Init() override
    {
        // register receive handle
        Receive("testMsg", &TestActor2::test_f);
        Receive("test_f", &TestActor2::test_f);
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
    void testAsync()
    {
        AsyncAfter(10, GetAID(), &TestActor1::testAsync);
    }

private:
    void test_f(litebus::AID from, std::string &&name, std::string &&body)
    {
        BUSLOG_INFO("Test From: {}, name: {}, body: {}", std::string(from), name, body);
        return;
    }
    void test1(std::string from, TestMessage msg)
    {
        BUSLOG_INFO("Receive test message");
        return;
    }

    virtual void Init() override
    {
        // register receive handle
        Receive("test_f", &TestActor1::test_f);

        BUSLOG_INFO(" send string message");
        std::string strMsg = "string = test send (to,name,strMsg)";
        Send(ACTOR2, "testMsg", std::move(strMsg));
        Send(ACTOR2, "testMsg", "test send (to,name,strMsg)");

        BUSLOG_INFO("dispatch message : return null");
        std::string data3("test local send, 3333");
        Async(ACTOR2, &TestActor2::test3, data3);

        std::string data4("test local send, 4444");
        Async(ACTOR2, &TestActor2::test4, data4);

        BUSLOG_INFO("dispatch message : return F");
        std::string data5("test local send, 5555");
        Async(ACTOR2, &TestActor2::test5).OnComplete(std::bind(callbackTest, ::_1));

        std::string data6("test local send, 6666");
        for (int i = 0; i < 3; i++) {
            Async(ACTOR2, &TestActor2::test6, i, data6).OnComplete(std::bind(callbackTest2, ::_1));
        }

        std::string data7("test local send, 7777");
        for (int i = 0; i < 3; i++) {
            Async(ACTOR2, &TestActor2::test7, i, data7);
        }

        const std::string data8("test local send, 8888");
        for (int i = 0; i < 3; i++) {
            Async(ACTOR2, &TestActor2::test8, i, data8).OnComplete(std::bind(callbackTest, ::_1));
        }

        std::string dataTimer1("test delay local send, timer");
        AsyncAfter(3 * litebus::SECONDS, ACTOR2, &TestActor2::testTimer1);
    }
};

class TemplateMessage {
public:
    virtual ~TemplateMessage(){};

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

        // (2) don't need return msg
    }
    void Init()
    {
    }
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
        std::string to("Worker2");
        litebus::Future<std::shared_ptr<TemplateMessage>> ret =
            litebus::Async(to, &Worker2::HandleTemplateMessage, msg);
        auto result = ret.Get(5);
        BUSLOG_INFO("HandleTemplateMessage return message: {}", result.IsSome() ? result.Get()->name : "timeout");
    }
};

class WaitActor : public litebus::ActorBase {
public:
    WaitActor() : ActorBase("waitactor")
    {
    }
    void Init()
    {
        this->Terminate();
    }
};

bool BoolRand()
{
    return (rand() % 100) > 50;
}

void wait()
{
    litebus::Await(ACTOR2);
    litebus::Await(ACTOR1);
}

bool g_terminal = false;
void sentTestMsg()
{
    while (!g_terminal) {
        litebus::SetActorStatus(ACTOR1, BoolRand());
        litebus::SetActorStatus(ACTOR2, BoolRand());

        std::unique_ptr<litebus::MessageBase> msg;
        // send to ACTOR2
        msg.reset(new litebus::MessageBase(ACTOR1, ACTOR2, "test_f", "dadsfdasf"));
        litebus::ActorMgr::Receive(std::move(msg));
        // send to ACTOR1
        msg.reset(new litebus::MessageBase(ACTOR2, ACTOR1, "test_f", "dadsfdasf"));
        litebus::ActorMgr::Receive(std::move(msg));

        AsyncAfter(10, ACTOR1, &TestActor1::testAsync);
        AsyncAfter(20, ACTOR2, &TestActor2::testAsync);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char **argv)
{
    std::thread sendThread = std::thread(sentTestMsg);

    for (int i = 0; i < 20; i++) {
        BUSLOG_INFO("start loop: {}", i);
        litebus::Initialize("127.0.0.1:8080");
        for (int j = 0; j < 20; j++) {
            litebus::Spawn(std::make_shared<TestActor2>(ACTOR2), BoolRand(), BoolRand());
            litebus::Spawn(std::make_shared<TestActor1>(ACTOR1), BoolRand(), BoolRand());
            std::thread *waitThread = new std::thread(wait);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            litebus::SetActorStatus(ACTOR1, true);
            litebus::SetActorStatus(ACTOR2, true);

            litebus::Terminate(ACTOR1);
            litebus::Terminate(ACTOR2);

            if (i * j % 2 == 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (i * j % 3 == 2) {
                litebus::Await(ACTOR1);
                litebus::Await(ACTOR2);
            }

            if (waitThread->joinable()) {
                if (j % 2 == 1) {
                    waitThread->join();
                } else {
                    waitThread->detach();
                }
            }
            delete waitThread;

            litebus::Await(ACTOR1);
            litebus::Await(ACTOR2);
        }

        litebus::ActorReference w2(new Worker2("Worker2"));
        litebus::Spawn(w2, BoolRand(), BoolRand());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        litebus::ActorReference w1(new Worker1("Worker1"));
        auto aid = litebus::Spawn(w1, BoolRand(), BoolRand());
        auto waitid = litebus::Spawn(std::make_shared<WaitActor>(), BoolRand(), BoolRand());
        litebus::SetActorStatus(aid, true);
        litebus::SetActorStatus(waitid, true);
        litebus::Await(waitid);
        litebus::TerminateAll();
        // litebus::Finalize();
    }
    g_terminal = true;

    if (sendThread.joinable()) {
        sendThread.join();
    }
    BUSLOG_INFO("The game is over!!!!!!!!! ");
    return 1;
}
