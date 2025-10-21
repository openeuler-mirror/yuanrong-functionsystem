#include <signal.h>

#include "actor/buslog.hpp"
#include "async/flag_parser_impl.hpp"
#include "httpd/http_actor.hpp"
#include "litebus.hpp"

#include <memory>

using namespace std;
using namespace litebus;
using namespace litebus::http;

const std::string g_http_enable_kmsg_api_server_name("HttpEnableKmsg_API_Server");
const std::string g_http_enable_kmsg_server_name("HttpEnableKmsg_Litebus_Server");

class HttpEnableKmsg : public litebus::flag::FlagParser {
public:
    HttpEnableKmsg()
    {
        AddFlag(&HttpEnableKmsg::server, "server", "Set server", "");
        AddFlag(&HttpEnableKmsg::delegate, "delegate", "Set delegate", "");
    }

    std::string server;
    std::string delegate;
};

class HttpEnableKmsgAPIServer : public HttpActor {
public:
    explicit HttpEnableKmsgAPIServer(const string &name) : HttpActor(name)
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("initiaize API Server..");
    }

    Future<Response> HandleHttpRequestUsingDelegate(const Request &request)
    {
        BUSLOG_INFO("Hi, i have got your message which visit /API_Server/api/v1...");
        return Response(ResponseCode::OK, "Hi, i have got your mesaage which visit /API_Server/api/v1...");
    }
};

class HttpEnableKmsgServer : public litebus::ActorBase {
public:
    HttpEnableKmsgServer(std::string name) : ActorBase(name)
    {
    }
    ~HttpEnableKmsgServer()
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("init LiteBus_Server...");
        Receive("HttpEnableKmsg", &HttpEnableKmsgServer::handleHttpEnableKmsg);
    }

    void handleHttpEnableKmsg(litebus::AID from, std::string &&type, std::string &&data)
    {
        // should not recv this msg
        BUSLOG_INFO("------receive data from: {}, type: {}", std::string(from), type);
        return;
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

    BUSLOG_INFO("start http server...");
    BUSLOG_INFO("argc= {},argv {} {}", argc, argv[1], argv[2]);

    HttpEnableKmsg flags;
    flags.ParseFlags(argc, argv);
    if (flags.server == "") {
        BUSLOG_ERROR(flags.Usage());
        return 0;
    }

    // Execute the following instruction in run_tests.sh
    litebus::Initialize(flags.server.c_str());

    string apiServerActorName(g_http_enable_kmsg_api_server_name);
    if (flags.delegate == g_http_enable_kmsg_api_server_name) {
        litebus::SetDelegate(apiServerActorName);
        apiServerActorName = flags.delegate;
    }

    BUSLOG_INFO("using http actor: {}", apiServerActorName);

    auto litebusServer = std::make_shared<HttpEnableKmsgServer>(g_http_enable_kmsg_server_name);

    litebus::Spawn(litebusServer);

    auto apiServer = std::make_shared<HttpEnableKmsgAPIServer>(apiServerActorName);
    litebus::Spawn(apiServer);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    litebus::Await(litebusServer);
    litebus::Await(apiServer);
    return 1;
}
