#include <signal.h>

#include "actor/buslog.hpp"
#include "async/async.hpp"
#include "litebus.hpp"

#include <memory>

#include "async/future.hpp"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"

const std::string g_client_name("Litebus_Client");
static const char *serverUrl;
static const char *clientUrl;

// using litebus::http
using litebus::http::HttpConnect;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::URL;

class LitebusClient : public litebus::ActorBase {
public:
    LitebusClient(std::string name) : ActorBase(name)
    {
    }

    ~LitebusClient()
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("init LiteBus_Server...");
        BUSLOG_INFO("send the first msg : RegisterExecutorMessage");

        Receive("RegisteredExecutorMessage", &LitebusClient::handleAck);

        litebus::AID to1;
        to1.SetUrl(serverUrl);
        to1.SetName("Litebus_Server");

        Link(to1);

        std::string msg_name1 = "RegisterExecutorMessage";
        std::string msg_data1 = "xyz";
        Send(to1, std::move(msg_name1), std::move(msg_data1), false);

        BUSLOG_INFO("send the first msg : ExecutorPingMessage");
        litebus::AID to2;
        to2.SetUrl(serverUrl);
        to2.SetName("Litebus_Server");
        std::string msg_name2 = "ExecutorPingMessage";
        std::string msg_data2 = "xyz";
        Send(to2, std::move(msg_name2), std::move(msg_data2), false);
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

    litebus::AID to;
    to.SetUrl(serverUrl);
    to.SetName("Litebus_Server");
    Request request;
    request.body = "xyz";
    URL url("http", to.GetIp(), to.GetPort(), "/api/v1");
    request.url = url;
    request.method = "POST";

    BUSLOG_INFO("start client to send post request]url={}", request.url.ip.Get());

    request.keepAlive = false;
    litebus::Future<Response> response1;
    litebus::Future<Response> response2;
    litebus::Future<Response> response3;
    response1 = litebus::http::LaunchRequest(request);
    response2 = litebus::http::LaunchRequest(request);
    response3 = litebus::http::LaunchRequest(request);
    int ret1 = response1.Get().retCode;
    int ret2 = response2.Get().retCode;
    int ret3 = response3.Get().retCode;

    BUSLOG_INFO("Return code1 is {}, code2 is {}, code3 is {}", ret1, ret2, ret3);
    BUSLOG_INFO("Start client to send keep-alive request]url= {}", request.url.ip.Get());
    request.keepAlive = true;
    litebus::Future<HttpConnect> connection = litebus::http::Connect(url);
    HttpConnect con = connection.Get();
    litebus::Future<Response> response4;
    litebus::Future<Response> response5;
    litebus::Future<Response> response6;
    response4 = con.LaunchRequest(request);
    response5 = con.LaunchRequest(request);
    response6 = con.LaunchRequest(request);
    int ret4 = response4.Get().retCode;
    int ret5 = response5.Get().retCode;
    int ret6 = response6.Get().retCode;
    BUSLOG_INFO("Return code4 is {}, code5 is {}, code6 is {}", ret4, ret5, ret6);
    BUSLOG_INFO("start client to send kmsg request .....");
    // Execute the following instruction in run_tests.sh
    //  litebus::Initialize(url);
    litebus::Initialize(clientUrl);

    auto litebusClient = std::make_shared<LitebusClient>(g_client_name);

    litebus::Spawn(litebusClient);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    litebus::Await(litebusClient);

    return 1;
}
