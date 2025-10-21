#include <signal.h>
#include <stdlib.h>
#include <chrono>
#include <vector>
#include <random>
#include <gtest/gtest.h>
#include "actor/actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"
#include "async/flag_parser_impl.hpp"
#include "litebus.hpp"
#include "actor/aid.hpp"

#define CHECKINTEVAL 500
#define TIMEOUTLINE 100
std::chrono::duration<int, std::micro> timeOutLine(100000);
char *testServerPath = (char *)"./server-stability";
int serverNum = 3;
int serActorNum = 10;
int clientActorNum = 50;
long maxPingTimes = 100000000;
// long maxPingTimes = 200;
std::vector<std::string> serverUrl;
std::map<std::string, std::string> serverActors;

class ClientActor : public litebus::ActorBase {
public:
    ClientActor(std::string name) : ActorBase(name)
    {
    }

    bool PingServer()
    {
        count++;
        litebus::AID to = SelectServerActor();
        std::string body(100, '-');
        pingTime = std::chrono::system_clock::now();
        BUSLOG_DEBUG("ping server]from={}, to={}, times={}", std::string(this->GetAID()), std::string(to), count);
        Send(to, "serverPing", std::move(body));

        return true;
    }

    bool PingLocal()
    {
        count++;
        litebus::AID to = SelectClientActor();
        std::string body(100, '-');
        pingTime = std::chrono::system_clock::now();
        BUSLOG_INFO("ping local]from={}, to={}, times={}", std::string(this->GetAID()), std::string(to), count);
        Send(to, "localPing", std::move(body));

        return true;
    }

    void HandleServerAck(litebus::AID from, std::string &&name, std::string &&body)
    {
        ackTime = std::chrono::system_clock::now();
        costTime = std::chrono::duration_cast<std::chrono::duration<int, std::micro>>(ackTime - pingTime);
        BUSLOG_DEBUG("ack]id={}, costTime={}, times={}, timeout={}", std::string(this->GetAID()), costTime.count(),
                     count, timeOutLine.count());
        if (0 == count % 100000) {
            auto m = ackTime.time_since_epoch();
            auto finish = std::chrono::duration_cast<std::chrono::microseconds>(m).count();
            auto n = startTime.time_since_epoch();
            auto start = std::chrono::duration_cast<std::chrono::microseconds>(n).count();
            BUSLOG_INFO("ack]from={}, to={}, times={}, totalTime={}", std::string(this->GetAID()), std::string(from),
                        count, finish - start);
        }

        if (costTime > timeOutLine) {
            auto m = ackTime.time_since_epoch();
            auto finish = std::chrono::duration_cast<std::chrono::microseconds>(m).count();
            auto n = startTime.time_since_epoch();
            auto start = std::chrono::duration_cast<std::chrono::microseconds>(n).count();
            BUSLOG_INFO(
                "ack timeout]id={}, start time={}, finish time={}, totalTime={}, costTime={}, timeout={}, count={}, "
                "maxPingPongTimes={}",
                std::string(this->GetAID()), start, finish, finish - start, costTime.count(), timeOutLine.count(),
                count, maxPingTimes);
            pingResult = 2;
        } else if (count >= maxPingTimes) {
            auto m = ackTime.time_since_epoch();
            auto finish = std::chrono::duration_cast<std::chrono::microseconds>(m).count();
            auto n = startTime.time_since_epoch();
            auto start = std::chrono::duration_cast<std::chrono::microseconds>(n).count();
            // auto totalTime = std::chrono::duration_cast<std::chrono::duration<int,std::micro>>(ackTime - startTime);
            BUSLOG_INFO(
                "ping finish]id={}, start time={}, finish time={}, totalTime={}, costTime={}, timeout={}, count={}, "
                "maxPingPongTimes={}",
                std::string(this->GetAID()), start, finish, finish - start, costTime.count(), timeOutLine.count(),
                count, maxPingTimes);
            pingResult = 1;
        } else {
            PingServer();
        }
    }

    void HandleLocalAck(litebus::AID from, std::string &&name, std::string &&body)
    {
        ackTime = std::chrono::system_clock::now();
        costTime = std::chrono::duration_cast<std::chrono::duration<int, std::micro>>(ackTime - pingTime);
        BUSLOG_DEBUG("ack]id={}, costTime={}, times={}, timeout={}", std::string(this->GetAID()), costTime.count(),
                     count, timeOutLine.count());
        if (0 == count % 100000) {
            auto m = ackTime.time_since_epoch();
            auto finish = std::chrono::duration_cast<std::chrono::microseconds>(m).count();
            auto n = startTime.time_since_epoch();
            auto start = std::chrono::duration_cast<std::chrono::microseconds>(n).count();
            BUSLOG_INFO("ack]from={}, to={}, times={}, totalTime={}", std::string(this->GetAID()), std::string(from),
                        count, finish - start);
        }

        if (costTime > timeOutLine) {
            auto m = ackTime.time_since_epoch();
            auto finish = std::chrono::duration_cast<std::chrono::microseconds>(m).count();
            auto n = startTime.time_since_epoch();
            auto start = std::chrono::duration_cast<std::chrono::microseconds>(n).count();
            BUSLOG_INFO(
                "ack timeout]id={}, start time={}, finish time={}, totalTime={}, costTime={}, timeout={}, count={}, "
                "maxPingPongTimes={}",
                std::string(this->GetAID()), start, finish, finish - start, costTime.count(), timeOutLine.count(),
                count, maxPingTimes);
            pingResult = 2;
        } else if (count >= maxPingTimes) {
            auto m = ackTime.time_since_epoch();
            auto finish = std::chrono::duration_cast<std::chrono::microseconds>(m).count();
            auto n = startTime.time_since_epoch();
            auto start = std::chrono::duration_cast<std::chrono::microseconds>(n).count();
            // auto totalTime = std::chrono::duration_cast<std::chrono::duration<int,std::micro>>(ackTime - startTime);
            BUSLOG_INFO(
                "ping finish]id={}, start time={}, finish time={}, totalTime={}, costTime={}, timeout={}, count={}, "
                "maxPingPongTimes={}",
                std::string(this->GetAID()), start, finish, finish - start, costTime.count(), timeOutLine.count(),
                count, maxPingTimes);
            pingResult = 1;
        } else {
            PingLocal();
        }
    }

    litebus::AID SelectServerActor()
    {
        std::random_device randServer;
        std::random_device randActor;
        int maxServer = serverUrl.size();
        std::string url = serverUrl[randServer() % maxServer];
        std::string name = serverActors[url] + "_" + std::to_string(randActor() % serActorNum);
        litebus::AID to(name, url);
        return to;
    }

    litebus::AID SelectClientActor();

    void HandleLocalPing(litebus::AID from, std::string &&name, std::string &&body)
    {
        Send(from, "localAck", std::move(body));
    }

    virtual void Init() override
    {
        BUSLOG_INFO("Init]id={}", std::string(this->GetAID()));
        Receive("serverAck", &ClientActor::HandleServerAck);
        Receive("localAck", &ClientActor::HandleLocalAck);

        Receive("localPing", &ClientActor::HandleLocalPing);

        startTime = std::chrono::system_clock::now();
        return;
    }
    long count = 0;
    std::chrono::system_clock::time_point pingTime;
    std::chrono::system_clock::time_point ackTime;
    std::chrono::system_clock::time_point startTime;
    std::chrono::duration<int, std::micro> costTime;
    int pingResult = 0;
};

std::vector<std::shared_ptr<ClientActor>> clientActorPool;
litebus::AID ClientActor::SelectClientActor()
{
    std::random_device randClientActor;
    unsigned int maxClient = clientActorPool.size();
    litebus::AID to = clientActorPool[randClientActor() % maxClient]->GetAID();
    return to;
}

class StabilityTest : public ::testing::Test {
protected:
    int port = 5000;
    std::vector<pid_t> pids;

    pid_t startServerByParams(char *path, char **args);
    bool startServers(int serNum, int serActorNum);
    void shutdownServer(pid_t pid);
    bool startClients(int clientNum);

    void SetUp()
    {
        BUSLOG_INFO("Stability Test start");
    }
    void TearDown()
    {
        for (unsigned int i = 0; i < pids.size(); i++) {
            shutdownServer(pids[i]);
            BUSLOG_INFO("shutting down server]i={}, pid={}", i, pids[i]);
            pids[i] = 0;
        }
        litebus::TerminateAll();
        BUSLOG_INFO("Stability Test finish");
    }
};

// listening local url and sending msg to remote url,if start succ.
pid_t StabilityTest::startServerByParams(char *path, char **args)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (execv(path, args) == -1) {
            BUSLOG_INFO("execve failed, errno: {}, args: {}, args[0]: {}", errno, *args, path);
        }
        return -1;
    } else {
        return pid;
    }
}

bool StabilityTest::startServers(int serNum, int serActorNum)
{
    std::string urlHead = "tcp://127.0.0.1:";
    char *connType = getenv("CONN_TYPE");
    if (nullptr != connType && 0 == strcmp(connType, "http")) {
        urlHead = "http://127.0.0.1:";
    }
    for (int i = 0; i < serNum; i++) {
        char *args[5];
        std::string serUrl = urlHead + std::to_string(port);
        serverUrl.push_back(serUrl);
        args[0] = (char *)serUrl.data();
        std::string serName = "server_" + std::to_string(i);
        serverActors[serUrl] = serName;
        args[1] = (char *)serName.data();
        args[2] = (char *)(std::to_string(serActorNum).data());
        std::string testType = "stability";
        args[3] = (char *)testType.data();
        args[4] = nullptr;
        pid_t pid = startServerByParams(testServerPath, args);
        if (pid > 0) {
            pids.push_back(pid);
            BUSLOG_INFO("start server sucess]pid={}", pid);
            port++;
        } else {
            BUSLOG_INFO("start server sucess]localurl={}, args: {}", serUrl, *args);
            return false;
        }
    }

    return true;
}

bool StabilityTest::startClients(int clientNum)
{
    for (int i = 0; i < clientNum; i++) {
        BUSLOG_INFO("start client]i={}", i);
        clientActorPool.push_back(std::make_shared<ClientActor>("client_" + std::to_string(i)));
        litebus::Spawn(clientActorPool[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

void StabilityTest::shutdownServer(pid_t pid)
{
    if (pid > 1) {
        kill(pid, SIGALRM);
        int status;
        waitpid(pid, &status, 0);
        BUSLOG_INFO("status = {}", status);
    }
}

TEST_F(StabilityTest, PingServer)
{
    serverNum = 5;
    serActorNum = 5;
    clientActorNum = 50;
    maxPingTimes = 100;

    bool serRet = startServers(serverNum, serActorNum);
    ASSERT_TRUE(serRet);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    bool clientRet = startClients(clientActorNum);
    ASSERT_TRUE(clientRet);
    bool testFlag = true;
    for (unsigned int i = 0; i < clientActorPool.size(); i++) {
        clientActorPool[i]->PingServer();
    }

    while (1) {
        unsigned int i = 0;
        for (i = 0; i < clientActorPool.size(); i++) {
            if (2 == clientActorPool[i]->pingResult) {
                BUSLOG_INFO("ping server failed]i={}, actor name={}", i, std::string(clientActorPool[i]->GetAID()));
                testFlag = false;
                break;
            } else if (0 == clientActorPool[i]->pingResult) {
                break;
            }
        }

        if ((false == testFlag) || (clientActorPool.size() == i)) {
            BUSLOG_INFO("test finish]testFlag={}, i={}", testFlag, i);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECKINTEVAL));
    }

    ASSERT_TRUE(testFlag);
}

TEST_F(StabilityTest, PingLocal)
{
    clientActorNum = 5;
    maxPingTimes = 200;

    bool clientRet = startClients(clientActorNum);
    ASSERT_TRUE(clientRet);
    bool testFlag = true;
    for (unsigned int i = 0; i < clientActorPool.size(); i++) {
        clientActorPool[i]->PingLocal();
    }

    while (1) {
        unsigned int i = 0;
        for (i = 0; i < clientActorPool.size(); i++) {
            if (2 == clientActorPool[i]->pingResult) {
                BUSLOG_INFO("ping server local]i={}, actor name={}", i, std::string(clientActorPool[i]->GetAID()));
                testFlag = false;
                break;
            } else if (0 == clientActorPool[i]->pingResult) {
                break;
            }
        }

        if ((false == testFlag) || (clientActorPool.size() == i)) {
            BUSLOG_INFO("test finish]testFlag={}, i={}", testFlag, i);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECKINTEVAL));
    }

    ASSERT_TRUE(testFlag);
}
