#include <signal.h>

#include "actor/buslog.hpp"
#include "actor/actormgr.hpp"
#include "actor/actor.hpp"
#include "async/async.hpp"
#include "litebus.hpp"
#include <gtest/gtest.h>

#include <memory>

#include "async/future.hpp"

const std::string g_httpkmsg_enable_client_name("Httpkmsg_Enable_Litebus_Client");
std::string apiServerUrl("127.0.0.1:44444");
std::string localUrl("127.0.0.1:22222");
const std::string apiServerName("HttpEnableKmsg_Litebus_Server");
static const char *serverUrl;
static const char *clientUrl;

using namespace litebus;

class HttpkmsgEnableClient : public litebus::ActorBase {
public:
    HttpkmsgEnableClient(std::string name) : ActorBase(name)
    {
    }

    ~HttpkmsgEnableClient()
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("init LiteBus_Server...");
        BUSLOG_INFO("send the first msg : HttpEnableKmsg");
        Receive("HttpEnableKmsg", &HttpkmsgEnableClient::handleAck);

        litebus::AID to;
        to.SetUrl(serverUrl);
        to.SetName("HttpEnableKmsg_Litebus_Server");
        std::string strMsg = "string = test send HttpEnableKmsg";
        Send(to, "HttpEnableKmsg", std::move(strMsg));

        BUSLOG_INFO("send the first msg : HttpEnableKmsg");
    }

    void handleAck(litebus::AID from, std::string &&type, std::string &&data)
    {
        BUSLOG_INFO("ack received");
    }

    void Exited(const litebus::AID &from)
    {
        BUSLOG_INFO("server has crashed, from {}", std::string(from));
    }
};

void my_handler(int signum)
{
    printf("received signal:%d\n", signum);
}

int main(int argc, char **argv)
{
    struct sigaction new_action, old_action;
    /* setup signal hander */
    new_action.sa_handler = my_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, nullptr, &old_action);
    if (old_action.sa_handler != SIG_IGN) {
        sigaction(SIGUSR1, &new_action, nullptr);
    }

    BUSLOG_INFO("start client .....");
    if (argc != 3) {
        BUSLOG_ERROR("parameter size error, input server and client address");
        return -1;
    }

    serverUrl = argv[1];
    clientUrl = argv[2];
    BUSLOG_INFO("start client to send kmsg request .....");
    litebus::Initialize(clientUrl);

    auto litebusClient = std::make_shared<HttpkmsgEnableClient>(g_httpkmsg_enable_client_name);

    litebus::Spawn(litebusClient);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    litebus::Await(litebusClient);

    return 1;
}
