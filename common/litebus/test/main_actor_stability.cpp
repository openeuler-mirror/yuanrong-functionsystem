#include <signal.h>
#include <stdlib.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "litebus.hpp"
#include "actor/buslog.hpp"
#include "executils.hpp"
#include "utils/os_utils.hpp"

int main(int argc, char **argv)
{
    // Initialize Google Test.
    testing::InitGoogleTest(&argc, argv);

    int port = litebus::find_available_port();
    litebus::os::SetEnv("LITEBUS_PORT", std::to_string(port));
    int serverPort = litebus::find_available_port();
    litebus::os::SetEnv("API_SERVER_PORT", std::to_string(serverPort));
    char *connType = getenv("CONN_TYPE");
    if (nullptr != connType && 0 == strcmp(connType, "http")) {
        BUSLOG_INFO("stability test conect type http!");
        litebus::Initialize("http://127.0.0.1:" + std::to_string(port));
    } else {
        litebus::Initialize("tcp://127.0.0.1:" + std::to_string(port));
    }

    int result = RUN_ALL_TESTS();

    litebus::Finalize();

    return result;
}
