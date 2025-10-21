#include <signal.h>

#include "actor/buslog.hpp"
#include "async/flag_parser_impl.hpp"
#include "httpd/http_actor.hpp"
#include "litebus.hpp"

#include <memory>

using namespace std;
using namespace litebus;
using namespace litebus::http;

const std::string g_api_server_name("API_Server");
const std::string g_server_name("Litebus_Server");

class HttpTestParser : public litebus::flag::FlagParser {
public:
    HttpTestParser()
    {
        AddFlag(&HttpTestParser::server, "server", "Set server", "");
        AddFlag(&HttpTestParser::delegate, "delegate", "Set delegate", "");
    }

    std::string server;
    std::string delegate;
};

class APIServer : public HttpActor {
public:
    explicit APIServer(const string &name) : HttpActor(name)
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("initiaize API Server..");
        // using delegate("API_Server", /API_Server/api/v1)
        AddRoute("/api/v1", &APIServer::HandleHttpRequestUsingDelegate);

        // do not using delegate(we set this actor's name as API_Server)
        AddRoute("/v1", &APIServer::HandleHttpRequestWithoutUsingDelegate);

        AddRoute("/resource", &APIServer::HandleQueryResource);
    }

    Future<Response> HandleHttpRequestUsingDelegate(const Request &request)
    {
        BUSLOG_INFO("Hi, i have got your message which visit /API_Server/api/v1...");
        return Response(ResponseCode::OK, "Hi, i have got your mesaage which visit /API_Server/api/v1...");
    }

    Future<Response> HandleHttpRequestWithoutUsingDelegate(const Request &request)
    {
        BUSLOG_INFO("Hi, i have got your message which visit /API_Server/v1...");
        return Response(ResponseCode::OK, "Hi, i have got your mesaage which visit /API_Server/v1...");
    }

    Future<Response> HandleQueryResource(const Request &request)
    {
        BUSLOG_INFO("handleQueryResource");
        litebus::http::HeaderMap headers = request.headers;

        // test application/json if user send a JSON content
        auto iter = headers.find("Content-Type");
        if (iter != headers.end() && headers["Content-Type"] == "application/json") {
            string response =
                "{"
                "  \"cpu\": 10,"
                "  \"mem\": 4"
                "}";

            return Ok(response, ResponseBodyType::JSON);
        }

        return BadRequest();
    }
};

class LitebusServer : public litebus::ActorBase {
public:
    LitebusServer(std::string name) : ActorBase(name)
    {
    }
    ~LitebusServer()
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("init LiteBus_Server...");
        Receive("RegisterExecutorMessage", &LitebusServer::handleRegister);

        Receive("ExecutorPingMessage", &LitebusServer::handlePing);
    }

    void handleRegister(litebus::AID from, std::string &&type, std::string &&data)
    {
        BUSLOG_INFO("receive data from: {}, type: {}", std::string(from), type);
        BUSLOG_INFO("receive register data: {}", data);
        Link(from);

        std::string msg_name2 = "RegisteredExecutorMessage";
        std::string msg_data2 = "xyzAck";
        Send(from, std::move(msg_name2), std::move(msg_data2), false);
        return;
    }

    void handlePing(litebus::AID from, std::string &&type, std::string &&data)
    {
        BUSLOG_INFO("receive data from: {}, type: {}", std::string(from), type);
        BUSLOG_INFO("receive ping data: {}", data);
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
    HttpTestParser flags;
    flags.ParseFlags(argc, argv);
    if (flags.server == "") {
        BUSLOG_ERROR(flags.Usage());
        return 0;
    }

    // Execute the following instruction in run_tests.sh
    litebus::Initialize(flags.server.c_str());

    string apiServerActorName(g_api_server_name);
    // TODO: Why HttpTestParser can not work well? --delegate parameter can not work
    if (flags.delegate == g_api_server_name) {
        litebus::SetDelegate(apiServerActorName);
        apiServerActorName = flags.delegate;
    }

    BUSLOG_INFO("using http actor: {}", apiServerActorName);
    auto litebusServer = std::make_shared<LitebusServer>(g_server_name);

    litebus::Spawn(litebusServer);

    auto apiServer = std::make_shared<APIServer>(apiServerActorName);
    litebus::Spawn(apiServer);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    litebus::Await(litebusServer);
    litebus::Await(apiServer);
    return 1;
}
