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

#include <atomic>
#include <iostream>
#include <string>

#include <thread>

#include <gtest/gtest.h>
#include <securec.h>
#include <signal.h>

#include <sys/resource.h>

#include "actor/actorapp.hpp"
#include "actor/iomgr.hpp"
#include "arpa/inet.h"
#include "async/async.hpp"
#include "litebus.hpp"
#include "udp/udp_adapter.hpp"
#include "udp/udpmgr.hpp"

using namespace std;

namespace litebus {
namespace UdpTest {
std::string recvSignature;

int recvNum = 0;
int exitmsg = 0;
UDPMgr *m_io = nullptr;
std::atomic<int> m_sendNum(0);
string m_localIP = "127.0.0.1";

string localurl1;
string localurl2;
string remoteurl1;
string remoteurl2;
void msgHandle(std::unique_ptr<MessageBase> &&msg)
{
    BUSLOG_INFO("UDPTest]recv msg, name: {}, signature: {}, from: {}, to: {}", msg->name, msg->signature,
                msg->from.operator std::string(), msg->to.operator std::string());
    recvSignature = msg->signature;
    recvNum++;
}

class UDPTest : public ::testing::Test {
protected:
    char *args1[4];
    char *args2[4];
    char *testServerPath;
    pid_t pid1;
    pid_t pid2;

    pid_t pids[100];

    void SetUp()
    {
        char *localpEnv = getenv("LITEBUS_IP");
        if (localpEnv != nullptr) {
            m_localIP = std::string(localpEnv);
        }

        BUSLOG_INFO("start");
        pid1 = 0;
        pid2 = 0;

        memset_s(pids, 100 * sizeof(pid_t), 0, 100 * sizeof(pid_t));
        recvNum = 0;
        exitmsg = 0;
        m_sendNum = 0;
        testServerPath = (char *)"./testUdpServer";
        args1[0] = (char *)testServerPath;
        // local url
        localurl1 = string("udp://" + m_localIP + ":2224");
        args1[1] = (char *)localurl1.data();
        // remote url
        remoteurl1 = string("udp://" + m_localIP + ":2225");
        args1[2] = (char *)remoteurl1.data();
        args1[3] = (char *)nullptr;
        args2[0] = (char *)testServerPath;
        localurl2 = string("udp://" + m_localIP + ":2225");
        args2[1] = (char *)localurl2.data();
        remoteurl2 = string("udp://" + m_localIP + ":2223");
        args2[2] = (char *)remoteurl2.data();
        args2[3] = (char *)nullptr;
        m_io = new UDPMgr();
        m_io->Init();
        m_io->RegisterMsgHandle(msgHandle);
        bool ret = m_io->StartIOServer("udp://" + m_localIP + ":2223", "udp://" + m_localIP + ":2223");
        BUSLOG_INFO("start server ret: {}", ret);
    }
    void TearDown()
    {
        BUSLOG_INFO("finish");
        shutdownUdpServer(pid1);
        shutdownUdpServer(pid2);
        pid1 = 0;
        pid2 = 0;

        if (pids[0]) {
            int i = 0;
            for (i = 0; i < 100; i++) {
                shutdownUdpServer(pids[i]);
                pids[i] = 0;
            }
        }
        m_io->Finish();
        delete m_io;
        m_io = nullptr;

        recvNum = 0;
        exitmsg = 0;
        m_sendNum = 0;
    }
    bool CheckRecvNum(int expectedRecvNum, int _timeout);
    bool ChecKEXITNum(int expectedExitNum, int _timeout);
    pid_t startUdpServer(char **args);
    void shutdownUdpServer(pid_t pid);

    void Link(string &_localUrl, string &_remoteUrl);
    void Reconnect(string &_localUrl, string &_remoteUrl);
    void Unlink(string &_remoteUrl);

public:
    static void SendMsg(const string &_localUrl, const string &_remoteUrl, int msgsize, bool remoteLink = false);
};

// listening local url and sending msg to remote url,if start succ.
pid_t UDPTest::startUdpServer(char **args)
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
void UDPTest::shutdownUdpServer(pid_t pid)
{
    if (pid > 1) {
        kill(pid, SIGALRM);
        int status;
        waitpid(pid, &status, 0);
        BUSLOG_INFO("status = {}", status);
    }
}

void UDPTest::SendMsg(const string &_localUrl, const string &_remoteUrl, int msgsize, bool remoteLink)
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
    message->signature = "signature-client";
    // cout << "to send"
    //    << endl;
    m_io->Send(std::move(message), remoteLink);
}

void UDPTest::Link(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_io->Link(from, to);
}

void UDPTest::Reconnect(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_io->Reconnect(from, to);
}

void UDPTest::Unlink(string &_remoteUrl)
{
    AID to("testserver", _remoteUrl);
    m_io->UnLink(to);
}
//_timeout: s
bool UDPTest::CheckRecvNum(int expectedRecvNum, int _timeout)
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

bool UDPTest::ChecKEXITNum(int expectedExitNum, int _timeout)
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

// server -> client -> server -> client
TEST_F(UDPTest, send1Msg)
{
    recvNum = 0;
    pid1 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    ASSERT_EQ("signature-server-0", recvSignature);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);
    SendMsg(from, to, 100);
    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    shutdownUdpServer(pid1);
    pid1 = 0;
}

TEST_F(UDPTest, sendMsgFail_80K)
{
    recvNum = 0;
    pid1 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":2225";
    SendMsg(from, to, 80 * 1024);
    SendMsg(from, to, 80 * 1024);
    ret = CheckRecvNum(2, 1);
    ASSERT_FALSE(ret);

    shutdownUdpServer(pid1);
    pid1 = 0;
}

// batch send 10 msgs
TEST_F(UDPTest, send10Msg)
{
    recvNum = 0;
    pid1 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":2225";
    int sendnum = 20;
    while (sendnum--) {
        SendMsg(from, to, 100);
    }

    ret = CheckRecvNum(11, 5);
    ASSERT_TRUE(ret);

    shutdownUdpServer(pid1);
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
        UDPTest::SendMsg(sendctx->from, sendctx->to, sendctx->sendSize);
    }
    delete sendctx;
    return nullptr;
}

// batch send; 1 thread,batch send 100 msgs
TEST_F(UDPTest, sendMsg100)
{
    recvNum = 0;
    exitmsg = 0;
    m_sendNum = 0;
    pid1 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":2225";
    int threadNum = 1;
    pthread_t threadIds[threadNum];
    int sendsize = 10;
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
            BUSLOG_INFO("pthread_join loopThread failed, i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ, i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum - 20, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    shutdownUdpServer(pid1);
    pid1 = 0;
}

// 10 threads send concurrently
TEST_F(UDPTest, SendConcurrently_10threads)
{
    recvNum = 0;
    exitmsg = 0;
    pid1 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":2225";
    int threadNum = 10;
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
        sendctx->sendNum = batch * 2;
        if (pthread_create(&threadIds[i], nullptr, SendThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed, i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ, i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    shutdownUdpServer(pid1);
    pid1 = 0;
}

// A->B1,B2...B100 -> C-> A
TEST_F(UDPTest, sendMsg_10Servers)
{
    int serverNum = 10;
    recvNum = 0;
    exitmsg = 0;
    int i = 0;
    int port = 3100;

    pid1 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    BUSLOG_INFO("***************sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    for (i = 0; i < serverNum; i++) {
        string localurl = "udp://" + m_localIP + ":" + std::to_string(port);
        args1[1] = (char *)localurl.data();
        pids[i] = startUdpServer(args1);
        port++;
    }

    ret = CheckRecvNum(serverNum + 1, 15);
    BUSLOG_INFO("***************sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":";
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
        sendctx->sendNum = batch * 2;
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
            BUSLOG_INFO("pthread_join loopThread failed, i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ, i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + serverNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendNum, recvNum);
    ASSERT_TRUE(ret);
    port = 3100;
    for (i = 0; i < serverNum; i++) {
        shutdownUdpServer(pids[i]);
        pids[i] = 0;
        port++;
    }

    shutdownUdpServer(pid1);
    pid1 = 0;
}

TEST_F(UDPTest, UDPMgr)
{
    UDPMgr *udpmgr = new UDPMgr();
    AID sAid("sAid");
    AID dAid("dAid");
    udpmgr->Link(sAid, dAid);
    udpmgr->UnLink(dAid);
    udpmgr->Reconnect(sAid, dAid);
    ASSERT_TRUE(udpmgr);
    delete udpmgr;
    udpmgr = nullptr;
}

TEST_F(UDPTest, recvLibprocessUdpMsg)
{
    if (m_localIP != "127.0.0.1") {
        return;
    }
    recvNum = 0;
    std::string libprocessMagic = "CSE.TCP";
    std::string name = "ping";
    std::string fromname = "fromname";
    std::string toname = "toname";
    std::string body = "libprocess";
    char *sendBuf = new char[80 * 1024];
    char *cur = sendBuf;
    uint32_t sendLen = 0;
    memcpy_s(cur, libprocessMagic.size(), libprocessMagic.data(), libprocessMagic.size());

    sendLen = sendLen + 48;
    cur = cur + 48;
    struct UCHeader *header = (struct UCHeader *)cur;
    header->msgNameLen = name.size();
    // header->type;
    header->srcIP = inet_addr("127.0.0.1");
    header->srcPort = 2223;
    header->srcPidLen = fromname.size();
    header->destIP = inet_addr("127.0.0.1");
    header->destPort = 2225;
    header->destPidLen = toname.size();
    header->dataSize = body.size();     // pb data size
    header->dataBodySize = 0;           // data body, can add data without pb
    header->packetFlag = 0x1213F4F5;    // packet flag for verifying validity

    sendLen = sendLen + sizeof(struct UCHeader);
    cur = cur + sizeof(struct UCHeader);

    memcpy_s(cur, name.size(), name.data(), name.size());
    sendLen = sendLen + name.size();
    cur = cur + name.size();

    memcpy_s(cur, fromname.size(), fromname.data(), fromname.size());
    sendLen = sendLen + fromname.size();
    cur = cur + fromname.size();

    memcpy_s(cur, toname.size(), toname.data(), toname.size());
    sendLen = sendLen + toname.size();
    cur = cur + toname.size();

    memcpy_s(cur, body.size(), body.data(), body.size());
    sendLen = sendLen + body.size();
    cur = cur + body.size();

    int fd;
    // create server socket
    fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    EXPECT_TRUE(fd);

    struct sockaddr_in toAddr;
    toAddr.sin_family = AF_INET;
    toAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    toAddr.sin_port = htons(2223);

    sendto(fd, sendBuf, sendLen, 0, (struct sockaddr *)&toAddr, sizeof(toAddr));

    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
}

TEST_F(UDPTest, HeartBeatMsg)
{
    recvNum = 0;
    pid1 = startUdpServer(args1);
    pid2 = startUdpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "udp://" + m_localIP + ":2223";
    string to = "udp://" + m_localIP + ":2225";
    BUSLOG_INFO("will add rule udp");
    m_io->AddRuleUdp("" + m_localIP + ":2223", 3);
    m_io->AddRuleUdp("" + m_localIP + ":2225", 3);
    SendMsg(from, to, 170);
    SendMsg(from, to, 110);
    SendMsg(from, to, 200);
    SendMsg(from, to, 300);
    SendMsg(from, to, 130);
    ret = CheckRecvNum(2, 5);
    SendMsg(to, from, 200);
    SendMsg(to, from, 300);
    SendMsg(to, from, 130);
    m_io->DelRuleUdp("" + m_localIP + ":2223", true);
    m_io->DelRuleUdp("" + m_localIP + ":2223", true);
    m_io->DelRuleUdp("" + m_localIP + ":2225", true);
    ASSERT_TRUE(ret);
    shutdownUdpServer(pid1);
    pid1 = 0;
}

TEST_F(UDPTest, CreateSocket)
{
    int ret;
    ret = UdpUtil::CreateSocket(100);
    ASSERT_TRUE(ret == -1);
}

TEST_F(UDPTest, ParseMsg)
{
    char *buf = (char *)malloc(5 * sizeof(char));
    uint32_t bufLen = 0;
    ASSERT_TRUE(nullptr == UdpUtil::ParseMsg(buf, bufLen));
    free(buf);
}

TEST_F(UDPTest, SetSocket)
{
    int ret = 0;
    ret = UdpUtil::SetSocket(200000);
    ASSERT_TRUE(ret == -1);
}

TEST_F(UDPTest, UDPMgrInit)
{
    std::unique_ptr<UDPMgr> io(new UDPMgr());

    constexpr unsigned long long SOFTVAL = 1024;
    constexpr unsigned long long HARDVAL = 4096;

    struct rlimit limits;
    limits.rlim_cur = SOFTVAL;
    limits.rlim_max = HARDVAL;

    BUSLOG_INFO("limit.rlim_cur: {}", limits.rlim_cur);
    BUSLOG_INFO("limit.rlim_max: {}", limits.rlim_max);

    limits.rlim_cur = 0;
    BUSLOG_INFO("limits.rlim_cur: {}", limits.rlim_cur);

    if (setrlimit(RLIMIT_NOFILE, &limits) != 0) {
        BUSLOG_ERROR("setrlimit failed; errno = {}", errno);
        return;
    }

    ASSERT_FALSE(io->Init());

    limits.rlim_cur = SOFTVAL;

    if (setrlimit(RLIMIT_NOFILE, &limits) != 0) {
        BUSLOG_ERROR("setrlimit failed; errno = {}", errno);
        return;
    }
    BUSLOG_INFO("After limit.rlim_cur: {}", limits.rlim_cur);
    BUSLOG_INFO("After limit.rlim_max: {}", limits.rlim_max);
}

// test udpmgr.cpp 269 line
TEST_F(UDPTest, StartIOServer)
{
    UDPMgr *udpmgr = new UDPMgr();
    string urlStr;
    string emptyStr;
    int ret = 1;
    udpmgr->StartIOServer(urlStr, emptyStr);
    ASSERT_TRUE(ret);
    delete udpmgr;
    udpmgr = nullptr;
}
}    // namespace UdpTest
}    // namespace litebus
