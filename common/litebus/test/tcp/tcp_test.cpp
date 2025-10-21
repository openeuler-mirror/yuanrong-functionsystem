#include <atomic>
#include <iostream>
#include <string>

#include <thread>

#include <gtest/gtest.h>
#include <signal.h>

#include <sys/resource.h>

#include <sys/types.h>
#include <dirent.h>

#include "litebus.hpp"

#define private public

#include "actor/actorapp.hpp"
#include "actor/iomgr.hpp"
#include "async/async.hpp"
#include "securec.h"
#include "tcp/tcpmgr.hpp"
#include "evloop/evloop.hpp"

using namespace std;

namespace litebus {
extern int EventLoopRun(EvLoop *, int);
extern void *EvloopRun(void *arg);
namespace TCPTest {
std::string recvSignature;
int recvNum = 0;
int exitmsg = 0;
TCPMgr *m_io = nullptr;
std::atomic<int> m_sendNum(0);
std::string m_recvBody;
string m_localIP = "127.0.0.1";
bool m_notRemote = false;

string localurl1;
string localurl2;
string remoteurl1;
string remoteurl2;
void msgHandle(std::unique_ptr<MessageBase> &&msg)
{
    if (msg->GetType() == MessageBase::Type::KEXIT) {
        BUSLOG_INFO("TCPTest]recv exit msg name {}, from: {}, to: {}", msg->name, std::string(msg->from),
                    std::string(msg->to));
        exitmsg++;
        return;
    }
    m_recvBody = msg->body;
    recvSignature = msg->signature;
    BUSLOG_INFO("TCPTest]recv msg name {}, signature: {}, from: {}, to: {}", msg->name, msg->signature,
                std::string(msg->from), std::string(msg->to));
    recvNum++;
}

class TCPTest : public ::testing::Test {
protected:
    char *args1[4];
    char *args2[4];
    char *testServerPath;
    pid_t pid1;
    pid_t pid2;

    pid_t pids[100];

    void SetUp()
    {
        char *locaNotRemoteEnv = getenv("LITEBUS_SEND_ON_REMOTE");
        if (locaNotRemoteEnv != nullptr) {
            m_notRemote = (std::string(locaNotRemoteEnv) == "true") ? true : false;
        }

        BUSLOG_INFO("start");
        pid1 = 0;
        pid2 = 0;

        memset_s(pids, 100 * sizeof(pid_t), 0, 100 * sizeof(pid_t));
        recvNum = 0;
        exitmsg = 0;
        m_sendNum = 0;
        testServerPath = (char *)"./testTcpServer";
        args1[0] = (char *)testServerPath;
        // local url
        localurl1 = string("tcp://" + m_localIP + ":2224");
        args1[1] = (char *)localurl1.data();
        // remote url
        remoteurl1 = string("tcp://" + m_localIP + ":2225");
        args1[2] = (char *)remoteurl1.data();
        args1[3] = (char *)nullptr;
        args2[0] = (char *)testServerPath;
        localurl2 = string("tcp://" + m_localIP + ":2225");
        args2[1] = (char *)localurl2.data();
        remoteurl2 = string("tcp://" + m_localIP + ":2223");
        args2[2] = (char *)remoteurl2.data();
        args2[3] = (char *)nullptr;

        m_io = new TCPMgr();
        m_io->Init();
        m_io->RegisterMsgHandle(msgHandle);
        bool ret = m_io->StartIOServer("tcp://" + m_localIP + ":2223", "tcp://" + m_localIP + ":2223");
        BUSLOG_INFO("start server ret: {}", ret);
    }
    void TearDown()
    {
        BUSLOG_INFO("finish");
        shutdownTcpServer(pid1);
        shutdownTcpServer(pid2);
        pid1 = 0;
        pid2 = 0;
        int i = 0;
        for (i = 0; i < 100; i++) {
            shutdownTcpServer(pids[i]);
            pids[i] = 0;
        }
        recvNum = 0;
        exitmsg = 0;
        m_sendNum = 0;
        m_io->Finish();
        delete m_io;
        m_io = nullptr;
    }
    bool CheckRecvNum(int expectedRecvNum, int _timeout);
    bool ChecKEXITNum(int expectedExitNum, int _timeout);
    pid_t startTcpServer(char **args);
    void shutdownTcpServer(pid_t pid);
    void KillTcpServer(pid_t pid);

    void Link(string &_localUrl, string &_remoteUrl);
    void Reconnect(string &_localUrl, string &_remoteUrl);
    void Unlink(const string &_remoteUrl);

public:
    static void SendMsg(const string &_localUrl, const string &_remoteUrl, int msgsize, bool remoteLink = false,
                        string body = "");
};

// listening local url and sending msg to remote url,if start succ.
pid_t TCPTest::startTcpServer(char **args)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (execv(args[0], args) == -1) {
            BUSLOG_INFO("execve failed, errno: {}, args: {}, args[0]: {}", errno, *args, args[0]);
        }
        return -1;
    } else {
        return pid;
    }
}
void TCPTest::shutdownTcpServer(pid_t pid)
{
    if (pid > 1) {
        kill(pid, SIGALRM);
        int status;
        waitpid(pid, &status, 0);
        BUSLOG_INFO("status = {}", status);
    }
}

void TCPTest::KillTcpServer(pid_t pid)
{
    if (pid > 1) {
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
        BUSLOG_INFO("status = {}", status);
    }
}

void TCPTest::SendMsg(const string &_localUrl, const string &_remoteUrl, int msgsize, bool remoteLink, string body)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);

    std::unique_ptr<MessageBase> message(new MessageBase());

    if (msgsize <= 0) {
        message->body = "";
    } else {
        message->body = string(msgsize, 'A');
    }
    message->name = "testname";
    message->from = from;
    message->to = to;
    message->signature = "test-signature-client";
    if (body != "") {
        message->body = body;
    }

    if (m_notRemote) {
        m_io->Send(std::move(message), remoteLink, true);
    } else {
        m_io->Send(std::move(message), remoteLink);
    }
}

void TCPTest::Link(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_io->Link(from, to);
}

void TCPTest::Reconnect(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_io->Reconnect(from, to);
}

void TCPTest::Unlink(const string &_remoteUrl)
{
    AID to("testserver", _remoteUrl);
    m_io->UnLink(to);
}
//_timeout: s
bool TCPTest::CheckRecvNum(int expectedRecvNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;    // us
    int usleepCount = 100000;                // 100ms

    while (timeout) {
        usleep(usleepCount);
        if (recvNum >= expectedRecvNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }
    return false;
}

bool TCPTest::ChecKEXITNum(int expectedExitNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;
    int usleepCount = 100000;

    while (timeout) {
        usleep(usleepCount);
        if (exitmsg >= expectedExitNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }

    return false;
}

TEST_F(TCPTest, StartServerFail)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();

    bool ret = io->StartIOServer("tcp://0:2223", "tcp://0:2223");
    BUSLOG_INFO("ret: {}", ret);
    ASSERT_FALSE(ret);

    ret = io->StartIOServer("tcp://" + m_localIP + ":2223", "tcp://" + m_localIP + ":2223");
    BUSLOG_INFO("ret: {}", ret);
    io->Finish();
    ASSERT_FALSE(ret);
}

TEST_F(TCPTest, StartServer2)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(msgHandle);
    bool ret = io->StartIOServer("tcp://" + m_localIP + ":2223", "tcp://" + m_localIP + ":2223");
    ASSERT_FALSE(ret);
    ret = io->StartIOServer("tcp://" + m_localIP + ":2224", "tcp://" + m_localIP + ":2224");
    BUSLOG_INFO("ret: {}", ret);
    io->Finish();
    ASSERT_TRUE(ret);
}

// server -> client -> server -> client
TEST_F(TCPTest, send1Msg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);  // from tcp server
    ASSERT_EQ("test-signature-server", recvSignature);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(TCPTest, sendInvalidMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 1024 * 1024 * 100 + 1);

    ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(TCPTest, sendMsg2)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    Unlink(to);
    ret = ChecKEXITNum(1, 5);
    SendMsg(from, to, 100);
    from = m_localIP + ":2223";
    to = m_localIP + ":2225";

    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 10);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// batch send 10 msgs
TEST_F(TCPTest, send10Msg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int sendnum = 10;
    while (sendnum) {
        SendMsg(from, to, 10 - sendnum);
        sendnum--;
    }

    ret = CheckRecvNum(11, 10);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// batch send 10 msgs
TEST_F(TCPTest, send10Msg2)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int sendnum = 10;
    while (sendnum--) {
        SendMsg(from, to, 8192);
    }

    ret = CheckRecvNum(11, 10);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(TCPTest, SendMsgCloseOnExec)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);

    SendMsg(from, to, 100, false, "CloseOnExec");

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    BUSLOG_INFO("************ ", m_recvBody);
    std::string recvBody = m_recvBody;

    pid2 = std::stoul(recvBody.substr(4));
    KillTcpServer(pid1);

    pid1 = startTcpServer(args2);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);
    SendMsg(from, to, 100);
    ret = CheckRecvNum(4, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    shutdownTcpServer(pid2);
    pid2 = 0;
    pid1 = 0;
}

// server -> client -> server -> client
TEST_F(TCPTest, sendMsgByRemoteLink)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100, true);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// link and send msg
TEST_F(TCPTest, link_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    shutdownTcpServer(pid1);
    pid1 = 0;
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);
}

// To test link is exist, Test steps: send,link,send;
TEST_F(TCPTest, link2_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// linker,Test steps: send,link,link,send
TEST_F(TCPTest, link3_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);
    Link(from, to);
    string from1 = "tcp://" + m_localIP + ":2222";
    Link(from1, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: reconnect,send
TEST_F(TCPTest, reconnect_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";

    Reconnect(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,send,reconnect,send
TEST_F(TCPTest, send_reconnect2_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Reconnect(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,send,shutdown server,send ,start server,reconnect,send
TEST_F(TCPTest, reconnect3_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    shutdownTcpServer(pid1);
    SendMsg(from, to, 100);
    pid1 = startTcpServer(args2);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);
    Reconnect(from, to);

    SendMsg(from, to, 100);

    ret = CheckRecvNum(4, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,unlink,send
TEST_F(TCPTest, unlink_sendMsg)
{
    recvNum = 0;
    exitmsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    Unlink(to);
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);
    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,link,send,unlink,send
TEST_F(TCPTest, unlink2_sendMsg)
{
    recvNum = 0;
    exitmsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    string from2 = "tcp://" + m_localIP + ":2222";
    Link(from2, to);
    SendMsg(from, to, 100);
    ret = CheckRecvNum(2, 5);

    Unlink(to);
    ret = ChecKEXITNum(2, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,send,shutdown server,send,start server,unlink,send
TEST_F(TCPTest, unlink3_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    shutdownTcpServer(pid1);
    SendMsg(from, to, 100);
    pid1 = startTcpServer(args2);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);
    Unlink(to);
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);

    ret = CheckRecvNum(4, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps:
TEST_F(TCPTest, unlink4_sendMsg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    AID from("testserver", "tcp://" + m_localIP + ":2223");
    AID to("testserver", "tcp://" + m_localIP + ":2225");
    m_io->Link(from, to);

    AID to2("testserver2", "tcp://" + m_localIP + ":2225");
    m_io->Link(from, to2);

    string fromurl = "tcp://" + m_localIP + ":2223";
    string tourl = "tcp://" + m_localIP + ":2225";

    SendMsg(fromurl, tourl, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    Unlink(tourl);
    ret = ChecKEXITNum(2, 5);
    ASSERT_TRUE(ret);

    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps:
TEST_F(TCPTest, unlink5)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    AID from("testserver", "tcp://" + m_localIP + ":2223");
    AID to("testserver", "tcp://" + m_localIP + ":2225");
    string fromurl = "tcp://" + m_localIP + ":2223";
    string tourl = "tcp://" + m_localIP + ":2225";

    Unlink(tourl);
    sleep(1);
    EXPECT_EQ(exitmsg, 0);

    m_io->Link(from, to);

    AID to2("testserver2", "tcp://" + m_localIP + ":2228");
    string tourl2 = "tcp://" + m_localIP + ":2228";
    m_io->Link(from, to2);
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(fromurl, tourl, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    Unlink(tourl);

    shutdownTcpServer(pid1);
    pid1 = 0;
}

struct SendMsgCtx {
    int sendNum;
    int sendSize;
    string from;
    string to;
};

void *SendThreadFunc(void *arg)
{
    if (!arg) {
        return nullptr;
    }
    SendMsgCtx *sendctx = (SendMsgCtx *)arg;

    int i = 0;
    for (i = 0; i < sendctx->sendNum; i++) {
        m_sendNum++;
        TCPTest::SendMsg(sendctx->from, sendctx->to, sendctx->sendSize);
    }
    delete sendctx;
    return nullptr;
}

// batch send; 1 thread,batch send 100 msgs
TEST_F(TCPTest, sendMsg100)
{
    recvNum = 0;
    exitmsg = 0;
    m_sendNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 1;
    pthread_t threadIds[threadNum];
    int sendsize = 100;
    int batch = 100;
    int i = 0;

    for (i = 0; i < threadNum; i++) {
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// batch send big packages(size=1M)
TEST_F(TCPTest, sendMsg10_1M)
{
    recvNum = 0;
    exitmsg = 0;
    m_sendNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 1;
    pthread_t threadIds[threadNum];
    int sendsize = 1024 * 1024;
    int batch = 10;
    int i = 0;

    for (i = 0; i < threadNum; i++) {
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// 100 threads send
TEST_F(TCPTest, SendConcurrently_100threads)
{
    recvNum = 0;
    exitmsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 100;
    pthread_t threadIds[threadNum];
    int sendsize = 2;
    int batch = 10;
    int i = 0;
    Link(from, to);
    for (i = 0; i < threadNum; i++) {
        sendsize = sendsize << 1;
        if (sendsize > 1048576) {
            sendsize = 2;
        }
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// 100 threads send concurrently
TEST_F(TCPTest, SendConcurrently2_100threads)
{
    recvNum = 0;
    exitmsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 100;
    pthread_t threadIds[threadNum];
    int sendsize = 2;
    int batch = 10;
    int i = 0;

    for (i = 0; i < threadNum; i++) {
        sendsize = sendsize << 1;
        if (sendsize > 1048576) {
            sendsize = 2;
        }
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// A->B1,B2...B100 -> C-> A
TEST_F(TCPTest, sendMsg_100Servers)
{
    int serverNum = 100;
    recvNum = 0;
    exitmsg = 0;
    int i = 0;
    int port = 3100;

    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    BUSLOG_INFO("***************sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    for (i = 0; i < serverNum; i++) {
        string localurl = "tcp://" + m_localIP + ":" + std::to_string(port);
        args1[1] = (char *)localurl.data();

        pids[i] = startTcpServer(args1);
        port++;
    }

    ret = CheckRecvNum(serverNum + 1, 15);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":";
    int threadNum = serverNum;
    pthread_t threadIds[threadNum];
    int sendsize = 2;
    int batch = 10;
    i = 0;
    port = 3100;
    for (i = 0; i < threadNum; i++) {
        sendsize = sendsize << 1;
        if (sendsize > 1048576) {
            sendsize = 2;
        }
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to + std::to_string(port);
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
        port++;
    }

    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + serverNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);
    port = 3100;
    for (i = 0; i < serverNum; i++) {
        string unlinkto = to + std::to_string(port);
        Unlink(unlinkto);
        shutdownTcpServer(pids[i]);
        pids[i] = 0;
        port++;
    }

    shutdownTcpServer(pid1);
    pid1 = 0;
}

void TestLinkerCallBack(const std::string &from, const std::string &to)
{
    BUSLOG_INFO("from: {}, to: {}", from, to);
}

TEST_F(TCPTest, LinkMgr)
{
    LinkMgr *linkMgr = new LinkMgr();
    Connection *conn = new Connection();
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = false;
    conn->from = "tcp://" + m_localIP + ":1111";
    conn->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn);
    const AID from("testserver", "tcp://" + m_localIP + ":1111");
    const AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd, from, to, TestLinkerCallBack);

    conn = new Connection();
    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = true;
    conn->from = "tcp://" + m_localIP + ":1113";
    conn->to = "tcp://" + m_localIP + ":1114";

    linkMgr->AddLink(conn);
    std::string toUrl = "tcp://" + m_localIP + ":1114";
    conn = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(TCPTest, LinkMgr2)
{
    LinkMgr *linkMgr = new LinkMgr();
    Connection *conn = new Connection();
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = false;
    conn->from = "tcp://" + m_localIP + ":1111";
    conn->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn);
    AID from("testserver", "tcp://" + m_localIP + ":1111");
    AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd, from, to, TestLinkerCallBack);

    LinkerInfo *linker = linkMgr->FindLinker(fd, from, to);
    ASSERT_TRUE(linker);
    linkMgr->DeleteAllLinker();
    linker = linkMgr->FindLinker(fd, from, to);
    ASSERT_FALSE(linker);

    conn = new Connection();
    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = true;
    conn->from = "tcp://" + m_localIP + ":1113";
    conn->to = "tcp://" + m_localIP + ":1114";

    linkMgr->AddLink(conn);
    std::string toUrl = "tcp://" + m_localIP + ":1114";
    conn = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(TCPTest, LinkMgr3)
{
    LinkMgr *linkMgr = new LinkMgr();
    // add link and linker
    Connection *conn1 = new Connection();
    int fd1 = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd1 < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn1->fd = fd1;
    conn1->isRemote = false;
    conn1->from = "tcp://" + m_localIP + ":1111";
    conn1->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn1);
    AID from("testserver", "tcp://" + m_localIP + ":1111");
    AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd1, from, to, TestLinkerCallBack);

    LinkerInfo *linker = linkMgr->FindLinker(fd1, from, to);
    ASSERT_TRUE(linker);

    // add remote link
    Connection *conn2 = new Connection();
    int fd2 = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd2 < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn2->fd = fd2;
    conn2->isRemote = true;
    conn2->from = "tcp://" + m_localIP + ":1111";
    conn2->to = "tcp://" + m_localIP + ":1112";
    linkMgr->AddLink(conn2);
    std::string toUrl = "tcp://" + m_localIP + ":1112";
    conn2 = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn2);

    // close remote link , we expect to trigger TestLinkerCallBack
    conn1 = linkMgr->ExactFindLink(conn1->to, false);
    ASSERT_TRUE(conn1->fd == fd1);
    conn2 = linkMgr->ExactFindLink(conn1->to, true);
    ASSERT_TRUE(conn2->fd == fd2);
    linkMgr->CloseConnection(conn2);
    ASSERT_TRUE(conn1->isExited);
    ASSERT_TRUE(linkMgr->linkers.empty());

    linker = linkMgr->FindLinker(fd1, from, to);
    ASSERT_FALSE(linker);
    linker = linkMgr->FindLinker(fd2, from, to);
    ASSERT_FALSE(linker);

    linkMgr->DeleteAllLinker();

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(TCPTest, LinkMgr4)
{
    LinkMgr *linkMgr = new LinkMgr();
    // add remote link and linker
    Connection *conn1 = new Connection();
    int fd1 = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd1 < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn1->fd = fd1;
    conn1->isRemote = true;
    conn1->from = "tcp://" + m_localIP + ":1111";
    conn1->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn1);
    AID from("testserver", "tcp://" + m_localIP + ":1111");
    AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd1, from, to, TestLinkerCallBack);

    LinkerInfo *linker = linkMgr->FindLinker(fd1, from, to);
    ASSERT_TRUE(linker);

    // add link
    Connection *conn2 = new Connection();
    int fd2 = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd2 < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn2->fd = fd2;
    conn2->isRemote = false;
    conn2->from = "tcp://" + m_localIP + ":1111";
    conn2->to = "tcp://" + m_localIP + ":1112";
    linkMgr->AddLink(conn2);
    std::string toUrl = "tcp://" + m_localIP + ":1112";
    conn2 = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn2);

    // close link , we expect to trigger TestLinkerCallBack
    conn1 = linkMgr->ExactFindLink(conn1->to, true);
    ASSERT_TRUE(conn1->fd == fd1);
    conn2 = linkMgr->ExactFindLink(conn1->to, false);
    ASSERT_TRUE(conn2->fd == fd2);
    linkMgr->CloseConnection(conn2);
    ASSERT_TRUE(conn1->isExited);
    ASSERT_TRUE(linkMgr->linkers.empty());

    linker = linkMgr->FindLinker(fd1, from, to);
    ASSERT_FALSE(linker);
    linker = linkMgr->FindLinker(fd2, from, to);
    ASSERT_FALSE(linker);

    linkMgr->DeleteAllLinker();

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(TCPTest, LinkMgr5)
{
    LinkMgr *linkMgr = new LinkMgr();
    // add remote link and linker
    Connection *conn1 = new Connection();
    int fd1 = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd1 < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn1->fd = fd1;
    conn1->isRemote = true;
    conn1->from = "tcp://" + m_localIP + ":1111";
    conn1->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn1);
    AID from("testserver", "tcp://" + m_localIP + ":1111");
    AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd1, from, to, TestLinkerCallBack);

    LinkerInfo *linker = linkMgr->FindLinker(fd1, from, to);
    ASSERT_TRUE(linker);

    // add link and linker
    Connection *conn2 = new Connection();
    int fd2 = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd2 < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn2->fd = fd2;
    conn2->isRemote = false;
    conn2->from = "tcp://" + m_localIP + ":1111";
    conn2->to = "tcp://" + m_localIP + ":1112";
    linkMgr->AddLink(conn2);
    linkMgr->AddLinker(fd2, from, to, TestLinkerCallBack);

    std::string toUrl = "tcp://" + m_localIP + ":1112";
    conn2 = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn2);

    // close link , we expect to trigger TestLinkerCallBack
    conn1 = linkMgr->ExactFindLink(conn1->to, true);
    ASSERT_TRUE(conn1->fd == fd1);
    conn2 = linkMgr->ExactFindLink(conn1->to, false);
    ASSERT_TRUE(conn2->fd == fd2);
    linkMgr->CloseConnection(conn2);
    ASSERT_TRUE(conn1->isExited);
    ASSERT_TRUE(linkMgr->linkers.empty());

    linker = linkMgr->FindLinker(fd1, from, to);
    ASSERT_FALSE(linker);
    linker = linkMgr->FindLinker(fd2, from, to);
    ASSERT_FALSE(linker);

    // close remote link , we don't expect to trigger TestLinkerCallBack
    linkMgr->CloseConnection(conn1);

    linkMgr->DeleteAllLinker();

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(TCPTest, EvbufMgr)
{
    EvbufMgr *evbufmgr = new EvbufMgr();
    ASSERT_TRUE(evbufmgr);
    delete evbufmgr;
    evbufmgr = nullptr;
}

TEST_F(TCPTest, TCPMgr)
{
    TCPMgr *tcpmgr = new TCPMgr();
    ASSERT_TRUE(tcpmgr);
    delete tcpmgr;
    tcpmgr = nullptr;
}

TEST_F(TCPTest, EvLoop)
{
    EvLoop *evLoop = new EvLoop();
    ASSERT_TRUE(evLoop);
    int ret = evLoop->AddFdEvent(-1, 1, nullptr, nullptr);
    ASSERT_FALSE(ret == BUS_OK);
    evLoop->AddFuncToEvLoop([ret] {
        // will not perform
        ASSERT_TRUE(ret);
    });
    ret = evLoop->Init("testTcpEvloop");
    ASSERT_TRUE(ret);

    void *threadResult;

    evLoop->StopEventLoop();

    pthread_join(evLoop->loopThread, &threadResult);

    close(evLoop->queueEventfd);
    evLoop->queueEventfd = -1;
    ret = false;
    evLoop->AddFuncToEvLoop([ret] {
        // will not perform
        ASSERT_TRUE(ret);
    });
    delete evLoop;
}

TEST_F(TCPTest, AddRuleUdp)
{
    TCPMgr *tcpmgr = new TCPMgr();
    bool ret = (tcpmgr->AddRuleUdp("123", 1) == 1);
    ASSERT_TRUE(ret);
    delete tcpmgr;
    tcpmgr = nullptr;
}

TEST_F(TCPTest, EvloopRun)
{
    auto arg = nullptr;
    ASSERT_TRUE(nullptr == EvloopRun(arg));
}

TEST_F(TCPTest, EvLoopInit)
{
    string emptyString;
    char nameOfThread[16];
    string expectName = "EventLoopThread";

    EvLoop *evLoop = new EvLoop();
    evLoop->Init(emptyString);
    int ret = pthread_getname_np(evLoop->loopThread, nameOfThread, 16);
    ASSERT_TRUE(ret == 0);
    string s = nameOfThread;
    ASSERT_TRUE(expectName == s);

    delete evLoop;
    evLoop = nullptr;
}

int getFileCount(const char *strDir)
{
    int num = 0;
    DIR *dp;
    struct dirent *entry;
    dp = opendir(strDir);
    if (!dp) {
        BUSLOG_INFO("failed to open dir!");
        return -1;
    }
    while ((entry = readdir(dp)) != nullptr) {
        if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) {
            continue;
        } else {
            num += 1;
        }
    };
    closedir(dp);
    return num - 1;
}

TEST_F(TCPTest, TCPMgrInit)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());

    constexpr unsigned long long SOFTVAL = 1024;
    constexpr unsigned long long HARDVAL = 4096;

    struct rlimit limit;
    limit.rlim_cur = SOFTVAL;
    limit.rlim_max = HARDVAL;

    BUSLOG_INFO("limit.rlim_cur: {}", limit.rlim_cur);
    BUSLOG_INFO("limit.rlim_max: {}", limit.rlim_max);

    pid_t initPid = getpid();
    string initPidStr = to_string(initPid);
    string dirStr = "/proc/" + initPidStr + "/fd";
    BUSLOG_INFO("dirStr: {}", dirStr);
    char *pDirStr = (char *)(dirStr.c_str());

    unsigned long long numberOfFd = getFileCount(pDirStr);
    BUSLOG_INFO("numberOfFd: {}", numberOfFd);

    limit.rlim_cur = numberOfFd;
    BUSLOG_INFO("limit.rlim_cur: {}", limit.rlim_cur);

    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        BUSLOG_ERROR("setrlimit failed; errno = {}", errno);
        return;
    }
    ASSERT_FALSE(io->Init());

    limit.rlim_cur = numberOfFd + 2;

    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        BUSLOG_ERROR("setrlimit failed; errno = {}", errno);
        return;
    }
    ASSERT_FALSE(io->Init());

    limit.rlim_cur = SOFTVAL;

    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        BUSLOG_ERROR("setrlimit failed; errno = {}", errno);
        return;
    }
    BUSLOG_INFO("After limit.rlim_cur: {}", limit.rlim_cur);
    BUSLOG_INFO("After limit.rlim_max: {}", limit.rlim_max);
}

TEST_F(TCPTest, StartIOServer)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(msgHandle);
    string emptyStr;
    bool ret = io->StartIOServer("tcp://" + m_localIP + ":2224", emptyStr);
    BUSLOG_INFO("start server ret: {}", ret);
    io->Finish();
    ASSERT_TRUE(ret);
}

// test tcpmgr.cpp 365
TEST_F(TCPTest, SocketEventHandler)
{
    Connection *c1 = new Connection();
    ASSERT_TRUE(c1 != nullptr);
    c1->recvEvloop = new EvLoop();
    int fd1 = 20000;
    uint32_t events1 = 10;
    ConnectionUtil::SocketEventHandler(fd1, events1, c1);
    ASSERT_TRUE(c1->connState == ConnectionState::DISCONNECTING);
    delete c1->recvEvloop;
    delete c1;
}

// test tcpmgr.cpp 337
TEST_F(TCPTest, EventCallBack)
{
    Connection *c1 = new Connection();
    ASSERT_TRUE(c1 != nullptr);
    c1->connState = ConnectionState::CONNECTED;
    TCPMgr::EventCallBack(c1);
    ASSERT_TRUE(c1->sendQueue.empty());
    delete c1;
    c1 = nullptr;
}

// test tcpmgr.cpp 493
TEST_F(TCPTest, OnAccept)
{
    int server = 0;
    uint32_t events = 32;
    void *tcpmgr = new TCPMgr();
    ASSERT_TRUE(tcpmgr != nullptr);

    tcpUtil::OnAccept(server, events, tcpmgr);
}

TEST_F(TCPTest, OnAcceptTest)
{
    int server = 1;
    uint32_t events = 0;
    TCPMgr *tcpmgr = new TCPMgr();
    EvLoop *recvEvloop = new EvLoop();
    tcpmgr->recvEvloop = recvEvloop;
    ASSERT_TRUE(tcpmgr != nullptr);

    tcpUtil::OnAccept(server, events, static_cast<void *>(tcpmgr));

    delete tcpmgr;
}

TEST_F(TCPTest, GetSocketErrTest)
{
    IOSockaddr addr;
    ASSERT_FALSE(SocketOperate::GetSockAddr("127.0.0.1:", addr));
}
}    // namespace TCPTest
}    // namespace litebus
