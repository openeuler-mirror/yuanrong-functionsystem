#include <time.h>
#include <chrono>

#include <signal.h>

#include "actor/actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"
#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/flag_parser_impl.hpp"

#include "litebus.hpp"

long gRunCount = 0;
using namespace litebus;
using namespace std;

static inline uint64_t get_time_us(void)
{
    uint64_t retval = 0;
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    retval = ts.tv_sec * 1000000;    // USECS_IN_SEC *NSECS_IN_USEC;
    retval += ts.tv_nsec / 1000;
    return retval;
}

class MyFlagParser : public litebus::flag::FlagParser {
public:
    MyFlagParser()
    {
        AddFlag(&MyFlagParser::type, "type", "client or server", "server");
        AddFlag(&MyFlagParser::serverUrl, "serverUrl", "Set server url", std::string());
        AddFlag(&MyFlagParser::serverActorNum, "serverActorNum", "Set server actor num", 1);
        AddFlag(&MyFlagParser::clientUrl, "clientUrl", "Set client url", std::string());
        AddFlag(&MyFlagParser::clientActorNum, "clientActorNum", "Set client actor num", 1);
        AddFlag(&MyFlagParser::sendCount, "sendCount", "Set sendCount for each client actor", 10000);
        AddFlag(&MyFlagParser::concurrency, "concurrency", "Set concurrency for each client actor", 250);
        AddFlag(&MyFlagParser::msgSize, "msgSize", "Set msgSize", 4096);
        AddFlag(&MyFlagParser::zExample, "zExample",
                "for example:\n"
                " ./throughput_performance --type=\"server\" --serverUrl=\"tcp://127.0.0.1:8080\" &\n"
                " ./throughput_performance --type=\"client\" "
                "--clientUrl=\"tcp://127.0.0.1:8081\" --serverUrl=\"tcp://127.0.0.1:8080\"\n ");
    }

    std::string type;
    std::string clientUrl;
    long clientActorNum;
    std::string serverUrl;
    long serverActorNum;
    long sendCount;
    long concurrency;
    long msgSize;
    std::string zExample;
};

class ClientActor : public litebus::ActorBase {
public:
    ClientActor(std::string name, std::string servername, MyFlagParser &flags)
        : ActorBase(name),
          serverUrl(flags.serverUrl),
          sendCount(flags.sendCount),
          concurrency(flags.concurrency),
          msgSize(flags.msgSize),
          sendNum(0),
          recvNum(0)
    {
        server = AID(servername, serverUrl);
    }
    void pong(const litebus::AID &from, std::string &&name, std::string &&body)
    {
        recvNum++;
        if (recvNum >= sendCount) {
            endTime = get_time_us();
            BUSLOG_INFO("{}, msgSize: {},concurrency: {}, sendCount: {}, tps: {}", std::string(GetAID()), msgSize,
                        concurrency, sendCount, sendCount * 1000000 / (endTime - startTime));
        }
        if (sendNum < sendCount) {
            string data = string(msgSize, '1');
            Send(server, "ping", std::move(data));
            sendNum++;
        }
    }

    void run(const litebus::AID &from, std::string &&name, std::string &&body)
    {
        BUSLOG_INFO("{} runing", std::string(GetAID()));

        Link(server);

        startTime = get_time_us();
        while (sendNum < concurrency) {
            Send(server, "ping", std::move(body));
            sendNum++;
        }
    }

    void DebugRecvNum()
    {
        BUSLOG_INFO("{}, recvNum:{}, sendNum:{}, sendCount:{}", std::string(GetAID()), recvNum, sendNum, sendCount);

        AsyncAfter(1000, GetAID(), &ClientActor::DebugRecvNum);
    }

    virtual void Init() override
    {
        Receive("pong", &ClientActor::pong);
        Receive("run", &ClientActor::run);
        return;
    }
    long count = 0;

private:
    AID server;
    uint64_t startTime;
    uint64_t endTime;
    std::string serverUrl;
    long sendCount;
    long concurrency;
    long msgSize;

    long sendNum;
    long recvNum;
};

class ServerActor : public litebus::ActorBase {
public:
    ServerActor(std::string name) : ActorBase(name), sendNum(0), recvNum(0)
    {
    }

    void DebugRecvNum()
    {
        BUSLOG_INFO("{}, recvNum: {}", std::string(GetAID()), recvNum);

        AsyncAfter(1000, GetAID(), &ServerActor::DebugRecvNum);
    }

    void ping(const litebus::AID &from, std::string &&name, std::string &&body)
    {
        recvNum++;
        string data = "ok";
        Send(from, "pong", std::move(data));
    }

    void shakeHands(const litebus::AID &from, std::string &&name, std::string &&body)
    {
        Send(from, "shakeHands", std::move(body));
    }

    virtual void Init() override
    {
        Receive("ping", &ServerActor::ping);
        Receive("shakeHands", &ServerActor::shakeHands);
        return;
    }
    long count = 0;

private:
    long sendNum;
    long recvNum;
};

class MainActor : public litebus::ActorBase {
public:
    MainActor(std::string name, std::string servername, MyFlagParser &flags)
        : ActorBase(name),
          serverIsReady(false),
          clientUrl(flags.clientUrl),
          clientActorNum(flags.clientActorNum),
          serverUrl(flags.serverUrl),
          msgSize(flags.msgSize)

    {
        server = AID(servername, serverUrl);
    }
    void CheckServer()
    {
        if (!serverIsReady) {
            string body = "shakeHands";
            Send(server, "shakeHands", std::move(body));
            AsyncAfter(1000, GetAID(), &MainActor::CheckServer);
        }
    }

    void shakeHands(const litebus::AID &from, std::string &&name, std::string &&body)
    {
        serverIsReady = true;
        int i;
        BUSLOG_INFO("server {} is ready", std::string(from));
        for (i = 0; i < clientActorNum; i++) {
            string body = string(msgSize, '1');
            string clientname = "client" + std::to_string(i);
            AID client = AID(clientname, clientUrl);
            BUSLOG_INFO("send run, client: {}", std::string(client));
            Send(client, "run", std::move(body));
        }
    }

    virtual void Init() override
    {
        Receive("shakeHands", &MainActor::shakeHands);
        AsyncAfter(1000, GetAID(), &MainActor::CheckServer);
        return;
    }
    long count = 0;

private:
    AID server;
    bool serverIsReady;
    uint64_t startTime;
    uint64_t endTime;
    std::string clientUrl;
    long clientActorNum;
    std::string serverUrl;
    long msgSize;
};

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    MyFlagParser flags;
    flags.ParseFlags(argc, argv);

    if (flags.help) {
        std::cout << flags.Usage() << std::endl;
        return 0;
    }

    if (flags.type == "" || flags.type.empty()) {
        std::cout << flags.Usage() << std::endl;
        return 0;
    }

    if (flags.type == "server" && flags.serverUrl == "") {
        std::cout << flags.Usage() << std::endl;
        return 0;
    }

    if (flags.type == "client" && (flags.clientUrl == "" || flags.serverUrl == "")) {
        std::cout << flags.Usage() << std::endl;
        return 0;
    }

    if (flags.type == "client") {
        litebus::Initialize(flags.clientUrl);
        std::vector<litebus::ActorReference> clients;
        int i;
        for (i = 0; i < flags.clientActorNum; i++) {
            string clientname = "client" + std::to_string(i);
            string servername = "server" + std::to_string(i % flags.serverActorNum);
            litebus::ActorReference clientActor1(new ClientActor(clientname, servername, flags));
            litebus::Spawn(clientActor1);
            clients.push_back(clientActor1);
        }
        sleep(1);
        string servername = "server" + std::to_string(flags.serverActorNum - 1);
        litebus::ActorReference mainActor(new MainActor("main", servername, flags));
        litebus::Spawn(mainActor);

        litebus::Await("main");
    } else {
        litebus::Initialize(flags.serverUrl);
        std::vector<litebus::ActorReference> servers;
        int i;
        for (i = 0; i < flags.serverActorNum; i++) {
            string servername = "server" + std::to_string(i);

            litebus::ActorReference serverActor(new ServerActor(servername));
            litebus::Spawn(serverActor);
            servers.push_back(serverActor);
        }
        litebus::Await("server0");
    }

    litebus::Finalize();
    BUSLOG_INFO("The game is over!!!!!!!!!");
    return 1;
}
