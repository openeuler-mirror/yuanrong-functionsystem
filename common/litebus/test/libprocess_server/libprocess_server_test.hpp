#ifndef _LIBPROCESS_SERVER_TEST_HPP_
#define _LIBPROCESS_SERVER_TEST_HPP_

#ifndef __WINDOWS__
#include <arpa/inet.h>
#endif    // __WINDOWS__

#ifndef __WINDOWS__
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif    // __WINDOWS__

#include <algorithm>
#include <deque>
#include <list>
#include <ostream>
#include <string>
#include <vector>

#include <process/address.hpp>
#include <process/authenticator.hpp>
#include <process/collect.hpp>
#include <process/defer.hpp>
#include <process/future.hpp>
#include <process/gmock.hpp>
#include <process/gtest.hpp>
#include <process/http.hpp>
#include <process/id.hpp>
#include <process/io.hpp>
#include <process/loop.hpp>

#ifdef USE_SSL_SOCKET
#include <process/jwt.hpp>
#endif    // USE_SSL_SOCKET

#include <process/owned.hpp>
#include <process/queue.hpp>
#include <process/socket.hpp>
#include <process/state_machine.hpp>

#include <process/ssl/gtest.hpp>

#include <stout/base64.hpp>
#include <stout/gtest.hpp>
#include <stout/hashset.hpp>
#include <stout/none.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>
#include <stout/os.hpp>
#include <stout/stringify.hpp>

#include <stout/tests/utils.hpp>

#include <process/decoder.hpp>
#include <process/encoder.hpp>

namespace authentication = process::http::authentication;
namespace http = process::http;
namespace ID = process::ID;
namespace inet = process::network::inet;
namespace inet4 = process::network::inet4;
namespace network = process::network;
#ifndef __WINDOWS__
namespace unix = process::network::unix;
#endif    // __WINDOWS__

using authentication::AuthenticationResult;
using authentication::Authenticator;
using authentication::BasicAuthenticator;
#ifdef USE_SSL_SOCKET
using authentication::JWT;
using authentication::JWTAuthenticator;
using authentication::JWTError;
#endif    // USE_SSL_SOCKET
using authentication::Principal;

using process::Break;
using process::Continue;
using process::ControlFlow;
using process::Failure;
using process::Future;
using process::Owned;
using process::PID;
using process::Process;
using process::Promise;
using process::READONLY_HTTP_AUTHENTICATION_REALM;
using process::READWRITE_HTTP_AUTHENTICATION_REALM;
using process::StateMachine;

using process::http::Request;
using process::http::Response;
using process::http::Scheme;
using process::http::URL;

using process::network::inet::Address;
using process::network::inet::Socket;

using process::network::internal::SocketImpl;

using process::defer;
using process::dispatch;
using process::spawn;
using process::terminate;

using std::deque;
using std::list;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

#include <signal.h>
#include <memory>
#include <thread>
#include <curl/curl.h>

#include <stout/flags.hpp>

using namespace std;

namespace process {
namespace network {
namespace openssl {

// Forward declare the `reinitialize()` function since we want to
// programatically change SSL flags during tests.
void reinitialize();

}    // namespace openssl
}    // namespace network
}    // namespace process

namespace litebus {
namespace libhttps {
const std::string apiServerName("APIServer");
const std::string apiServerUrl("tcp://127.0.0.1:2227");
static const std::string g_libprocess_server_name("Libprocess_Server");

static const std::string g_libprocess_server_url("tcp://127.0.0.1:44441");

class Flags : public virtual flags::FlagsBase {
public:
    Flags();
    ~Flags()
    {
    }
    static Flags *getInstance();
    bool ssl_enabled;
    bool out_with_https;
    std::string log_file;
    std::string log_dir;
};

class ServerProcess : public Process<ServerProcess> {
public:
    ServerProcess() : ProcessBase(g_libprocess_server_name)
    {
    }
    virtual ~ServerProcess()
    {
    }

protected:
    virtual void initialize()
    {
        install("Ping", &ServerProcess::ping);
        //
        route("/BigSize", ::None(), [this](const process::http::Request &request) {
            std::string rspbody = std::string(1024 * 512, 'a');
            return process::http::OK(rspbody);
        });
        route("/post", ::None(),
              [this](const process::http::Request &request) { return process::http::OK("responsed post"); });

        route("/postback", ::None(),
              [this](const process::http::Request &request) { return handlePostBack(request); });

        route("/get", ::None(),
              [this](const process::http::Request &request) { return process::http::OK("responsed get"); });
    }

private:
    void ping(const process::UPID &from, const string &body)
    {
        link(from);
        BUSLOG_INFO("send pong from {}, body: {}", from, body);

        string msg_data = "this is a pong message form libprocess!";

        send(from, "Pong", msg_data.c_str(), msg_data.size());
    }

    Future<Response> handlePostBack(const process::http::Request &request)
    {
        BUSLOG_INFO("post back to litebus...");
        string scheme;
        if (Flags::getInstance()->out_with_https) {
            scheme = "https";
        } else {
            scheme = "http";
        }

        process::http::URL url(scheme, "127.0.0.1", 2227, "/APIServer/api/v1");
        process::Future<process::http::Response> future = process::http::post(url);
        if (future.get().code == 200) {
            BUSLOG_INFO("post 200 back to litebus...");
            return process::http::OK("responsed postback");
        } else {
            BUSLOG_INFO("post 400 back to litebus...");
            return process::http::BadRequest("responsed postback");
        }
    }
};

void UnSetLibProcessHttpsEnv();
void SetLibProcessHttpsEnv();
}    // namespace libhttps
}    // namespace litebus
namespace process {
bool reinitialize(const ::Option<string> &delegate = ::None(),
                  const ::Option<string> &readonlyAuthenticationRealm = ::None(),
                  const ::Option<string> &readwriteAuthenticationRealm = ::None());
bool initialize(const ::Option<string> &delegate, const ::Option<string> &readonlyAuthenticationRealm,
                const ::Option<string> &readwriteAuthenticationRealm);

}    // namespace process

#endif
