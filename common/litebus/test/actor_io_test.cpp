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
#include <actor/iomgr.hpp>
#include <iostream>
#include <string>
#include <tcp/tcpmgr.hpp>
#include <thread>
#include "actor/actorapp.hpp"
#include "async/async.hpp"
#include "litebus.hpp"

using namespace std;

namespace litebus_1 {

class ActorIoMgrTest : public ::testing::Test {
protected:
    char *args1[4];
    char *args2[4];
    char *testServerPath;
    pid_t pid1;
    pid_t pid2;

    void SetUp()
    {
        BUSLOG_INFO("start");
        pid1 = 0;
        pid2 = 0;
        testServerPath = (char *)"./testTcpServer";
        args1[0] = (char *)testServerPath;
        args1[1] = (char *)"tcp://127.0.0.1:2224";
        args1[2] = (char *)"tcp://127.0.0.1:2225";
        args1[3] = (char *)nullptr;
        args2[0] = (char *)testServerPath;
        args2[1] = (char *)"tcp://127.0.0.1:2225";
        args2[2] = (char *)"tcp://127.0.0.1:2223";
        args2[3] = (char *)nullptr;
    }
    void TearDown()
    {
        shutdownTcpServer(pid1);
        shutdownTcpServer(pid2);
        pid1 = 0;
        pid2 = 0;
    }
    bool CheckRecvNum(int expectedRecvNum, int _timeout);
    bool CheckExitNum(int expectedExitNum, int _timeout);
    pid_t startTcpServer(char **args);
    void shutdownTcpServer(pid_t pid);
    void SendMsg(string &_localUrl, string &_remoteUrl, int msgsize);
    void Link(string &_localUrl, string &_remoteUrl);
    void Reconnect(string &_localUrl, string &_remoteUrl);
    void Unlink(string &_remoteUrl);
};

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

void TCPTest::SendMsg(string &_localUrl, string &_remoteUrl, int msgsize)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);

    std::unique_ptr<MessageBase> message(new MessageBase());
    string data(msgsize, 'A');
    message->name = "testname";
    message->from = from;
    message->to = to;
    message->body = data;
    cout << "to send" << endl;
    m_io->send(std::move(message));
}

void TCPTest::Link(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_io->link(from, to);
}

void TCPTest::Reconnect(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_io->Reconnect(from, to);
}

void TCPTest::Unlink(string &_remoteUrl)
{
    AID to("testserver", _remoteUrl);
    m_io->unLink(to);
}
//_timeout: s
bool TCPTest::CheckRecvNum(int expectedRecvNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;
    int usleepCount = 100000;

    while (timeout) {
        usleep(usleepCount);
        if (recvNum >= expectedRecvNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }
    return false;
}

bool TCPTest::CheckExitNum(int expectedExitNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;
    int usleepCount = 100000;

    while (timeout) {
        usleep(usleepCount);
        if (recvNum >= expectedExitNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }

    return false;
}

TEST_F(TCPTest, StartServerFail)
{
    bool ret = m_io->startIOServer("tcp://127.0.0.1:2223", "tcp://127.0.0.1:2223");
    BUSLOG_INFO("ret: {}", ret);
    ASSERT_FALSE(ret);
}

TEST_F(TCPTest, StartServer2)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->init();
    io->registerMsgHandle(msgHandle);
    bool ret = io->startIOServer("tcp://127.0.0.1:2223", "tcp://127.0.0.1:2223");
    ASSERT_FALSE(ret);
    ret = io->startIOServer("tcp://127.0.0.1:2224", "tcp://127.0.0.1:2224");
    BUSLOG_INFO("ret: {}", ret);
    io.reset();
    ASSERT_TRUE(ret);
}

#if 1
// server -> client -> server -> client
TEST_F(TCPTest, send1Msg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://127.0.0.1:2223";
    string to = "tcp://127.0.0.1:2225";
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}
TEST_F(TCPTest, send10Msg)
{
    recvNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://127.0.0.1:2223";
    string to = "tcp://127.0.0.1:2225";
    int sendnum = 10;
    while (sendnum--) {
        SendMsg(from, to, 100);
    }

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(TCPTest, link_sendMsg)
{
    recvNum = 0;
    args2[1] = (char *)"tcp://127.0.0.1:2226";
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://127.0.0.1:2223";
    string to = "tcp://127.0.0.1:2226";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(TCPTest, send_reconnect_sendMsg)
{
    recvNum = 0;
    args2[1] = (char *)"tcp://127.0.0.1:2227";
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://127.0.0.1:2223";
    string to = "tcp://127.0.0.1:2227";
    SendMsg(from, to, 100);
    Reconnect(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(TCPTest, link_unlink_sendMsg)
{
    recvNum = 0;
    exitmsg = 0;
    args2[1] = (char *)"tcp://127.0.0.1:2228";
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://127.0.0.1:2223";
    string to = "tcp://127.0.0.1:2228";
    Link(from, to);
    Unlink(to);
    ret = CheckExitNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);
    ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

#endif

}    // namespace litebus_1
