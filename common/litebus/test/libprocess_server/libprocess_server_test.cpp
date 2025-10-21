#include "libprocess_server_test.hpp"

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

#include <assert.h>

using namespace std;
constexpr auto PASSWDLEN = 512;

extern void set_passwd_for_decrypting_private_key(const char *passwd_key, size_t passwd_len);

namespace litebus {
namespace libhttps {

Flags::Flags()
{
    add(&Flags::ssl_enabled, "ssl_enabled", "open ssl", true);
    add(&Flags::out_with_https, "out_with_https", "send out http request with https", true);

    add(&Flags::log_dir, "log_dir",
        "log_dir: the log file directory, \n"
        "e.g. /tmp/libprocess_test",
        "/tmp/libprocess_test");

    add(&Flags::log_file, "log_file",
        "log_file: the log file name, \n"
        "e.g. libprocess",
        "libprocess");
}

Flags *Flags::getInstance()
{
    static Flags *instance = nullptr;
    if (nullptr == instance) {
        instance = new (std::nothrow) Flags();
    }
    return instance;
}

void UnSetLibProcessHttpsEnv()
{
    // This unsets all the SSL environment variables. Necessary for
    // ensuring a clean starting slate between tests.
    os::unsetenv("LIBPROCESS_SSL_ENABLED");
    os::unsetenv("LIBPROCESS_SSL_CERT_FILE");
    os::unsetenv("LIBPROCESS_SSL_KEY_FILE");
    os::unsetenv("LIBPROCESS_SSL_VERIFY_CERT");
    os::unsetenv("LIBPROCESS_SSL_REQUIRE_CERT");
    os::unsetenv("LIBPROCESS_SSL_VERIFY_DEPTH");
    os::unsetenv("LIBPROCESS_SSL_CA_DIR");
    os::unsetenv("LIBPROCESS_SSL_CA_FILE");
    os::unsetenv("LIBPROCESS_SSL_CIPHERS");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_SSL_V3");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_TLS_V1_0");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_TLS_V1_1");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_TLS_V1_2");
    os::unsetenv("LIBPROCESS_IP");
    os::unsetenv("LIBPROCESS_PORT");
}

void SetLibProcessHttpsEnv()
{
    // This unsets all the SSL environment variables. Necessary for
    // ensuring a clean starting slate between tests.
    os::unsetenv("LIBPROCESS_SSL_ENABLED");
    os::unsetenv("LIBPROCESS_SSL_CERT_FILE");
    os::unsetenv("LIBPROCESS_SSL_KEY_FILE");
    os::unsetenv("LIBPROCESS_SSL_VERIFY_CERT");
    os::unsetenv("LIBPROCESS_SSL_REQUIRE_CERT");
    os::unsetenv("LIBPROCESS_SSL_VERIFY_DEPTH");
    os::unsetenv("LIBPROCESS_SSL_CA_DIR");
    os::unsetenv("LIBPROCESS_SSL_CA_FILE");
    os::unsetenv("LIBPROCESS_SSL_CIPHERS");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_SSL_V3");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_TLS_V1_0");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_TLS_V1_1");
    os::unsetenv("LIBPROCESS_SSL_ENABLE_TLS_V1_2");
    os::unsetenv("LIBPROCESS_IP");
    os::unsetenv("LIBPROCESS_PORT");

    const char *sslSandBox = getenv("LITEBUS_SSL_SANDBOX");

    assert(sslSandBox != nullptr);

    std::string keyPath = std::string(sslSandBox) + "moca_keys/MSP_File";
    std::string certPath = std::string(sslSandBox) + "moca_keys/MSP.pem.cer";
    std::string rootCertPath = std::string(sslSandBox) + "moca_keys/CA.pem.cer";
    std::string rootCertDirPath = std::string(sslSandBox) + "moca_keys/";

    BUSLOG_INFO("keyPath is {},certPath is {}, rootCertPath is {}", keyPath, certPath, rootCertPath);
    std::map<std::string, std::string> environment;

    if (Flags::getInstance()->ssl_enabled) {
        environment["LIBPROCESS_SSL_ENABLED"] = "1";
        environment["LIBPROCESS_SSL_KEY_FILE"] = keyPath;
        environment["LIBPROCESS_SSL_CERT_FILE"] = certPath;
        environment["LIBPROCESS_SSL_REQUIRE_CERT"] = "1";
        environment["LIBPROCESS_SSL_VERIFY_CERT"] = "1";
        environment["LIBPROCESS_SSL_CA_DIR"] = rootCertDirPath;
        environment["LIBPROCESS_SSL_CA_FILE"] = rootCertPath;
    }

    environment["LIBPROCESS_IP"] = "127.0.0.1";
    environment["LIBPROCESS_PORT"] = "44555";

    // Copy the given map into the clean slate.
    foreachpair(const std::string &name, const std::string &value, environment)
    {
        os::setenv(name, value);
    }

    std::string privateKey = "Msp-4102";    // TODO: use a Flags instead
    set_passwd_for_decrypting_private_key(privateKey.c_str(), privateKey.length());
}
}    // namespace libhttps
}    // namespace litebus
