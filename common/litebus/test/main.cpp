#include <iostream>
#include <memory>
#include <string>

#include <cstdlib>

#include <signal.h>

#include <gtest/gtest.h>

#include <stdlib.h>
#include "actor/buslog.hpp"
#include "litebus.hpp"
#include "litebus.h"
#include <string.h>
#define private public
#include "actor/sysmgr_actor.hpp"
#include "logs/api/provider.h"
#include "logs/sdk/logger_provider.h"
#include "logs/sdk/log_param_parser.h"
#include "executils.hpp"
#include "utils/os_utils.hpp"

std::string g_Protocol;
std::string g_localip = "127.0.0.1";
bool g_ipv6 = false;
const std::string NODE_NAME = "node";
const std::string MODEL_NAME = "model";
const std::string LOG_CONFIG_JSON = R"(
{
  "filepath": ".",
  "level": "DEBUG",
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
    if (argc > 1) {
        if (strcmp(argv[1], "-v") == 0) {
            std::cout << "litebus version : V100.001" << std::endl;
            return 0;
        } else if (strcmp(argv[1], "-f") == 0) {
            litebus::Finalize();
            return 0;
        }
    }

    auto globalParam = LogsSdk::GetGlobalLogParam(LOG_CONFIG_JSON);
    auto param = LogsSdk::GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    auto lp = std::make_shared<LogsSdk::LoggerProvider>(globalParam);
    lp->CreateYrLogger(param);
    LogsApi::Provider::SetLoggerProvider(lp);

    BUSLOG_INFO("trace: enter main---------");

    // Initialize Google Test.
    testing::InitGoogleTest(&argc, argv);

    // litebus::SetLogPID(PID_HASEN_COMMON);
    litebus::SysMgrActor::linkRecycleDuration = 250;

    const char *envPro = getenv("PROTOCOL");
    setenv("LITEBUS_THREADS", "10", false);

    if (envPro == nullptr) {
        g_Protocol = "ALL";
    } else {
        g_Protocol = envPro;
    }

    char *localpEnv = getenv("LITEBUS_IP");
    if (localpEnv != nullptr) {
        g_localip = std::string(localpEnv);
        g_ipv6 = true;
    }

    int port = litebus::find_available_port();
    litebus::os::SetEnv("LITEBUS_PORT", std::to_string(port));
    int serverPort = litebus::find_available_port();
    litebus::os::SetEnv("API_SERVER_PORT", std::to_string(serverPort));
    if (g_Protocol == "tcp") {
        BUSLOG_INFO("Run litebus on tcp");
        auto result = litebus::Initialize("tcp://" + g_localip + ":" + std::to_string(port), "",
                                          "udp://" + g_localip + ":" + std::to_string(port));
        if (result != BUS_OK) {
            return -1;
        }
    } else {
        g_Protocol = "tcp";
        auto result = litebus::Initialize("tcp://" + g_localip + ":" + std::to_string(port), "",
                                          "udp://" + g_localip + ":" + std::to_string(port + 1));
        if (result != BUS_OK) {
            return -1;
        }
        BUSLOG_INFO("Run litebus on tcp/udp");
    }

    int result = RUN_ALL_TESTS();

    litebus::Finalize();
    LitebusFinalizeC();

    BUSLOG_INFO("trace: exit main---------");
    return result;
}
