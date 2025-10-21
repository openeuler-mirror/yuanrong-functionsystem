#ifndef __HTTP_TEST_HPP__
#define __HTTP_TEST_HPP__

#include <memory>

#include <curl/curl.h>
#include <signal.h>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "actor/buslog.hpp"
#include "actor/actormgr.hpp"
#include "async/future.hpp"
#include "async/async.hpp"
#include "litebus.hpp"
#include "exec/exec.hpp"
#include "exec/reap_process.hpp"
#include "utils/os_utils.hpp"

#ifdef SSL_ENABLED
#include "ssl/openssl_wrapper.hpp"
#endif

#define private public    // hack complier
#define protected public
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "httpd/http_actor.hpp"
#include "httpd/http_iomgr.hpp"
#include "httpd/http_sysmgr.hpp"
#undef private
#undef protected

using namespace litebus;
using namespace litebus::http;
using namespace std;

using litebus::http::HeaderMap;
using litebus::http::HttpConnect;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::ResponseCode;
using litebus::http::URL;

extern std::string g_localip;
extern bool g_ipv6;

// Global vars
const std::string SYSMGR_ACTOR_NAME = "SysManager";
extern int recvKmsgNum;
extern int recvKhttpNum;
const std::string apiServerName("APIServer");
extern std::string apiServerUrl;
extern std::string localUrl;
extern std::string httpCurlUrl;
extern std::string httpsCurlUrl;

#ifdef LIBPROCESS_INTERWORK_ENABLED
const std::string libprocess_log_dir("/tmp/libprocess_test");
const std::string libprocess_log_file("libprocess");
#endif

inline std::string GetEnv(const char* name, const std::string& defaultVal)
{
    if (const char *env = std::getenv(name)) {
        return env;
    }
    return defaultVal;
}

inline uint16_t GetPortEnv(const char* name, uint16_t defaultPort)
{
    try {
        std::string env = GetEnv(name, std::to_string(defaultPort));
        size_t pos;
        int port = std::stoi(env, &pos);

        if (pos != env.length()) {
            throw std::invalid_argument("Invalid characters in port");
        }

        if (port < 1 || port > 65535) {
            throw std::out_of_range("Port out of valid range");
        }

        return static_cast<uint16_t>(port);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("Environment variable ") + name + " error: " + e.what());
    }
}

class APIServer : public HttpActor {
public:
    explicit APIServer(const string &name) : HttpActor(name)
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("Initiaize API Server.");
        Option<std::string> sServerPort = os::GetEnv("API_SERVER_PORT");
        apiServerUrl = g_localip + ":" + sServerPort.Get();
        httpCurlUrl = g_localip + ":" + sServerPort.Get();
        httpsCurlUrl = g_localip + ":" + sServerPort.Get();
        Option<std::string> sPort = os::GetEnv("LITEBUS_PORT");
        localUrl = g_localip + ":" + sPort.Get();
        BUSLOG_INFO("Initiaize API Server. localUrl: {}, apiServerUrl: {}, httpCurlUrl: {}, httpsCurlUrl: {}",
                    localUrl, apiServerUrl, httpCurlUrl, httpsCurlUrl);

        if (g_ipv6) {
            // ipv6
            httpCurlUrl = std::string() + "http://[" + g_localip + "]:" + sServerPort.Get();;
            httpsCurlUrl = std::string() + "https://[" + g_localip + "]:" + sServerPort.Get();;
        }

        AddRoute("/api/v1", &APIServer::HandleHttpRequest);
        AddRoute("/api/v2", &APIServer::HandleHttpRequest1);
        AddRoute("/api/v3", &APIServer::HandleHttpRequest2);
        AddRoute("/api/v4", &APIServer::HandleHttpRequest3);

        AddRoute("/", &APIServer::HandleDefaultHttpRequest);

        Receive("PingMessage", &APIServer::handleHttpMsg);
    }

    Future<Response> HandleHttpRequest(const Request &request);

    Future<Response> HandleHttpRequest1(const Request &request);

    Future<Response> HandleHttpRequest2(const Request &request);

    Future<Response> HandleHttpRequest3(const Request &request);

    Future<Response> HandleDefaultHttpRequest(const Request &request);

    void handleHttpMsg(litebus::AID from, std::string &&type, std::string &&data);

    void CheckRequestClient(const Request &request);
};

class HTTPTest : public ::testing::Test {
protected:
    void SetUp();

    void TearDown();

    bool CheckRecvKmgNum(int expectedNum, int _timeout);
    bool CheckRecvReqNum(int expectedNum, int _timeout);
    bool CheckLinkNum(int expectedLinkNum, int _timeout);

public:
    std::shared_ptr<APIServer> apiServer = nullptr;

#ifdef LIBPROCESS_INTERWORK_ENABLED
    Try<std::shared_ptr<Exec>> libprocessServer;
#endif
};

#endif
