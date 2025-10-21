
#include <iostream>

#include <thread>

#include <signal.h>

#include <memory>
#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include "logs/api/provider.h"
#include "logs/sdk/logger_provider.h"
#include "logs/sdk/log_param_parser.h"

#include "actor/iomgr.hpp"
#include "tcp/tcpmgr.hpp"
#include "ssl/openssl_wrapper.hpp"
#include "httpd/http_iomgr.hpp"
using namespace litebus;
using namespace std;

namespace litebus {
namespace openssl {
bool SslInitInternal();
}
}    // namespace litebus

int recvNum = 0;
std::shared_ptr<IOMgr> io = nullptr;
string localIP = "127.0.0.1";
string localUrl = "";
string remoteUrl = "";

string localUrl2 = "";
string remoteUrl2 = "";

char *args1[4];
char *testServerPath;
pid_t pid1;

// listening local url and sending msg to remote url,if start succ.
pid_t startTcpServer()
{
    testServerPath = (char *)"./testSslServer";
    args1[0] = (char *)testServerPath;
    // local url
    localUrl2 = string("tcp://" + localIP + ":2229");
    args1[1] = (char *)localUrl2.data();
    // remote url
    remoteUrl2 = string("tcp://" + localIP + ":1111");
    args1[2] = (char *)remoteUrl2.data();
    args1[3] = (char *)nullptr;

    pid_t pid = fork();
    if (pid == 0) {
        if (execv(args1[0], args1) == -1) {
            BUSLOG_INFO("execve failed, errno: {}, args: {}, args[0]: {}", errno, *args1, args1[0]);
        }
        return -1;
    } else {
        return pid;
    }
}

void msgHandle(std::unique_ptr<MessageBase> &&msg)
{
    if (msg->GetType() == MessageBase::Type::KEXIT) {
        BUSLOG_DEBUG("server recv exit msg name {}, from: {}, to: {}, body: {}", msg->name, std::string(msg->from),
                     std::string(msg->to), msg->body);
        return;
    }
    BUSLOG_DEBUG("server recv msg name {}, from: {}, to: {}", msg->name, std::string(msg->from), std::string(msg->to));
    recvNum++;

    AID from("testserver", localUrl);
    AID to("testserver", remoteUrl);
    std::unique_ptr<MessageBase> message(new MessageBase());

    message->name = "testname";
    message->from = from;
    message->to = to;
    message->body = "testbody";

    if (msg->body == "CloseOnExec") {
        pid_t pid = startTcpServer();
        message->body = "PID:" + std::to_string(pid);
    }

    cout << "to send" << endl;
    io->Send(std::move(message));
}

void kill_handler(int sig)
{
    std::cout << "***********kill_handler, exit!************" << endl;
    io->Finish();
    exit(0);
}

const std::string NODE_NAME = "server";
const std::string MODEL_NAME = "server";
const std::string LOG_CONFIG_JSON = R"(
{
  "filepath": ".",
  "level": "ERROR",
  "rolling": {
    "maxsize": 100,
    "maxfiles": 1
  },
  "async": {
    "logBufSecs": 30,
    "maxQueueSize": 1048510,
    "threadCount": 1
  },
  "alsologtostderr": true,
  "stdLogLevel": "ERROR"
}
)";
namespace LogsSdk = observability::sdk::logs;
namespace LogsApi = observability::api::logs;

int main(int argc, char **argv)
{
    auto lp = std::make_shared<LogsSdk::LoggerProvider>(LogsSdk::GetGlobalLogParam(LOG_CONFIG_JSON));
    lp->CreateYrLogger(LogsSdk::GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false));
    LogsApi::Provider::SetLoggerProvider(lp);

    struct sigaction act;

    act.sa_handler = kill_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGTERM, &act, nullptr);
    sigaction(SIGALRM, &act, nullptr);
    signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        BUSLOG_ERROR("check arg, argc: {}, argv[1]: {}", argc, argv[1]);
        return 0;
    }

    localUrl = argv[1];
    remoteUrl = argv[2];
    BUSLOG_DEBUG("************ localUrl: {}, remoteUrl: {}", localUrl, remoteUrl);
    char *localpEnv = getenv("LITEBUS_IP");
    if (localpEnv != nullptr) {
        localIP = std::string(localpEnv);
    }
    litebus::HttpIOMgr::EnableHttp();
    const char *sslSandBox = getenv("LITEBUS_SSL_SANDBOX");

    std::string keyPath = std::string(sslSandBox) + "moca_keys/MSP_File";
    std::string certPath = std::string(sslSandBox) + "moca_keys/MSP.pem.cer";
    std::string rootCertPath = std::string(sslSandBox) + "moca_keys/CA.pem.cer";
    std::string rootCertDirPath = std::string(sslSandBox) + "moca_keys/";
    std::string decryptPath = std::string(sslSandBox) + "moca_keys/ct/";
    BUSLOG_INFO("keyPath is {}", keyPath);
    BUSLOG_INFO("certPath is {}", certPath);
    BUSLOG_INFO("rootCertPath is {}", rootCertPath);
    BUSLOG_INFO("decryptPath is {}", decryptPath);
    std::map<std::string, std::string> environment;
    environment["LITEBUS_SSL_ENABLED"] = "1";
    environment["LITEBUS_SSL_KEY_FILE"] = keyPath;
    environment["LITEBUS_SSL_CERT_FILE"] = certPath;
    environment["LITEBUS_SSL_REQUIRE_CERT"] = "1";
    environment["LITEBUS_SSL_VERIFY_CERT"] = "1";
    environment["LITEBUS_SSL_CA_DIR"] = rootCertDirPath;
    environment["LITEBUS_SSL_CA_FILE"] = rootCertPath;
    environment["LITEBUS_SSL_DECRYPT_TYPE"] = "0";
    environment["LITEBUS_SSL_DECRYPT_DIR"] = decryptPath;
    auto pKey = std::string("Msp-4102");
    litebus::SetPasswdForDecryptingPrivateKey(pKey.c_str(), pKey.length());
    bool sslInitialized = litebus::openssl::SslInit();
    if (!sslInitialized) {
        BUSLOG_ERROR("ssl initialize failed");
    }

    FetchSSLConfigFromMap(environment);
    litebus::openssl::SslInitInternal();

    io.reset(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(msgHandle);
    bool ret = io->StartIOServer(localUrl, localUrl);
    BUSLOG_INFO("start ssl server success: {}", ret);

    AID from("testserver", localUrl);
    AID to("testserver", remoteUrl);
    std::unique_ptr<MessageBase> message(new MessageBase());

    message->name = "testname";
    message->from = from;
    message->to = to;
    message->body = "testbody";
    BUSLOG_INFO("send message to {}", std::string(to));
    io->Send(std::move(message));

    sleep(100);
    litebus::openssl::SslFinalize();
    BUSLOG_INFO("ssl server end");
}
