#include <signal.h>
#include <chrono>
#include <vector>
#include "actor/actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"
#include "async/flag_parser_impl.hpp"
#include "litebus.hpp"
#include "timer/timertools.hpp"
#include "actor/iomgr.hpp"
#include "tcp/tcpmgr.hpp"
#ifdef UDP_ENABLED
#include "udp/udpmgr.hpp"
#endif
#ifdef HTTP_ENABLED
#include "httpd/http_iomgr.hpp"
#endif

class ServerActor : public litebus::ActorBase {
public:
    ServerActor(std::string name) : ActorBase(name)
    {
    }
    void HandleServerPing(litebus::AID from, std::string &&name, std::string &&body)
    {
        Send(from, "serverAck", std::move(body));
    }

    virtual void Init() override
    {
        Receive("serverPing", &ServerActor::HandleServerPing);
        return;
    }
    long count = 0;
};

void ReceiveBufferMsg(std::unique_ptr<litebus::MessageBase> &&msg)
{
    BUSLOG_INFO("start handle buffer msg and sleep!");
    std::this_thread::sleep_for(std::chrono::milliseconds(100000));
    BUSLOG_INFO("end handle buffer msg and weakup!");
}

void StartTestServer(const std::string &url, const std::string &advUrl, litebus::IOMgr::MsgHandler handle)
{
    std::string protocol = "tcp";
    std::shared_ptr<litebus::IOMgr> io = nullptr;
    std::string advertiseUrl = advUrl;

    if (advertiseUrl == "") {
        advertiseUrl = url;
    }

    size_t index = url.find("://");
    if (index != std::string::npos) {
        protocol = url.substr(0, index);
    }
    if (protocol == "http") {
        protocol = "tcp";
    }

    std::shared_ptr<litebus::IOMgr> ioMgrRef = litebus::ActorMgr::GetIOMgrRef(protocol);
    if (ioMgrRef != nullptr) {
        BUSLOG_ERROR("{} is exist, url: {}, advertiseUrl: {}", protocol, url, advertiseUrl);
        return;
    }

    if (protocol == "tcp") {
        BUSLOG_INFO("create tcp iomgr, url: {}, advertiseUrl: {}", url, advertiseUrl);
        io.reset(new litebus::TCPMgr());
    }
#ifdef UDP_ENABLED
    else if (protocol == "udp") {
        BUSLOG_INFO("create udp iomgr, url: {}, advertiseUrl: {}", url, advertiseUrl);
        io.reset(new litebus::UDPMgr());
    }
#endif
    else {
        BUSLOG_INFO("unsupport protocol {}", protocol);
        return;
    }

    io->Init();
    bool ok = io->StartIOServer(url, advertiseUrl);
    if (!ok) {
        BUSLOG_ERROR("server start failed, url: {}, advertiseUrl: {}", url, advertiseUrl);
        BUS_EXIT("init IOServer err.");
        return;
    }

    io->RegisterMsgHandle(handle);
    litebus::ActorMgr::GetActorMgrRef()->AddUrl(protocol, advertiseUrl);
    litebus::ActorMgr::GetActorMgrRef()->AddIOMgr(protocol, io);

    return;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    if (strlen(argv[0]) == 0 || strlen(argv[1]) == 0) {
        BUSLOG_INFO("args 0 or 1 is null");
        return 0;
    }

    std::string url(argv[0]);
    std::string serverName(argv[1]);
    std::string actorNumStr(argv[2]);
    std::string testType(argv[3]);
    int actorNum;
    std::stringstream ss;
    ss << actorNumStr;
    ss >> actorNum;

    // initialize the litebus
    BUSLOG_INFO("Stability test server starting]url={}, serverName={}, actorNum={}, testType={}", url, serverName,
                actorNum, testType);
    if (testType == "flow_control") {
#ifdef UDP_ENABLED
        StartTestServer("udp://127.0.0.1:4000", "udp://127.0.0.1:4000", ReceiveBufferMsg);
#endif
        StartTestServer("tcp://127.0.0.1:4100", "tcp://127.0.0.1:4100", ReceiveBufferMsg);
    } else {
        litebus::Initialize(url);
    }

    std::vector<std::shared_ptr<ServerActor>> serverPoll;

    for (int i = 0; i < actorNum; i++) {
        serverPoll.push_back(std::make_shared<ServerActor>(serverName + "_" + std::to_string(i)));
    }

    // record start time
    auto start = std::chrono::system_clock::now();
    for (int i = 0; i < actorNum; i++) {
        litebus::Spawn(serverPoll[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (int i = 0; i < actorNum; i++) {
        litebus::Await(serverPoll[i]);
    }

    // record end time
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end - start;
    BUSLOG_INFO("use time: {} s", diff.count());
    litebus::Finalize();
    BUSLOG_INFO("Stability test is over!!!!!!!!! ");

    return 1;
}
