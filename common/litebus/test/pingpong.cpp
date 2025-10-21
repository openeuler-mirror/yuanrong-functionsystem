#include <signal.h>
#include <chrono>

#include "actor/actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"
#include "async/flag_parser_impl.hpp"

#include "litebus.hpp"

long g_runCount = 0;

class PingPongActor : public litebus::ActorBase {
public:
    PingPongActor(std::string name) : ActorBase(name)
    {
    }

    void pong(litebus::AID from, std::string &&, std::string &&body)
    {
        BUSLOG_DEBUG("pingpong body size: {}", body.size());
        body.append("*");
        Send(from, "ping", std::move(body));
    }

    void ping(litebus::AID from, std::string &&, std::string &&body)
    {
        count++;
        if (count >= g_runCount) {
            this->Terminate();
        }
        BUSLOG_DEBUG("pingpong body size: {}, times: {}", body.size(), count);
        Send(from, "pong", std::move(body));
    }

    virtual void Init() override
    {
        Receive("ping", &PingPongActor::ping);
        Receive("pong", &PingPongActor::pong);
        return;
    }
    long count = 0;
};

class MyFlagParser : public litebus::flag::FlagParser {
public:
    MyFlagParser()
    {
        AddFlag(&MyFlagParser::url1, "url1", "Set  url 1", std::string());

        AddFlag(&MyFlagParser::url2, "url2", "Set url 2", std::string());

        AddFlag(&MyFlagParser::type, "type", "ping or pong", "pong");

        AddFlag(&MyFlagParser::to, "to", "to url", std::string());

        AddFlag(&MyFlagParser::runCount, "count", "Set runCount", 10000);
        AddFlag(&MyFlagParser::msgSize, "size", "Set msgSize", 512);
    }

    std::string url1;
    std::string url2;
    std::string type;
    std::string to;
    long msgSize;
    long runCount;
};

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    MyFlagParser flags;
    flags.ParseFlags(argc, argv);
    if (flags.url1 == "" || flags.url1.empty()) {
        BUSLOG_INFO(flags.Usage());
        return 0;
    }

    // initialize the litebus
    BUSLOG_INFO("The game is starting...");
    litebus::Initialize(flags.url1);
    // litebus::Initialize(flags.url1, "", flags.url2, "");
    litebus::ActorReference appActor1(new PingPongActor("pingpong"));
    litebus::Spawn(appActor1);
    // record start time
    auto start = std::chrono::system_clock::now();

    // send ping msg to pingpong actor
    if (flags.type == "ping") {
        g_runCount = flags.runCount;
        std::string body(flags.msgSize, '-');
        std::unique_ptr<litebus::MessageBase> msg;
        msg.reset(new litebus::MessageBase(flags.to, "pingpong", "ping", std::move(body)));
        litebus::ActorMgr::Receive(std::move(msg));
    }

    litebus::Await("pingpong");

    // record end time
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end - start;
    BUSLOG_INFO("pingpong times: {}, msgsize: {}, time: {}s", g_runCount, flags.msgSize, diff.count());
    litebus::Finalize();
    BUSLOG_INFO("The game is over!!!!!!!!! ");
    return 1;
}
