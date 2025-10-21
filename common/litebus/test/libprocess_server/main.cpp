#include <stout/os.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <cstdlib>

#include <signal.h>

#include <stdlib.h>

#include "actor/buslog.hpp"
#include "libprocess_server_test.hpp"
using namespace litebus::libhttps;
std::shared_ptr<litebus::libhttps::ServerProcess> apiServer = nullptr;

int main(int argc, char **argv)
{
    litebus::libhttps::Flags liteProcessFlags;
    Try<flags::Warnings> load = Flags::getInstance()->load(None(), argc, argv);
    if (load.isError()) {
        cerr << load.error() << endl;
        return -1;
    }
    const std::string nodeName = "node_name";
    const std::string moduleName = "module_name";
    litebus::InitLog(nodeName, moduleName);
    // check the log directory, create directory recursively if it doesn't exist
    Try<Nothing> mkdir = os::mkdir(Flags::getInstance()->log_dir);
    if (mkdir.isError()) {
        cerr << "Create log directory fail]logDir=" << Flags::getInstance()->log_dir << ",errno=" << mkdir.error()
             << endl;
        return -1;
    }

    struct stat dirStat;
    int checkdir = stat(Flags::getInstance()->log_dir.c_str(), &dirStat);
    if (!checkdir && !(S_ISDIR(dirStat.st_mode))) {
        cerr << "Create log directory fail]logDir=" << Flags::getInstance()->log_dir;
        return -1;
    }

    BUSLOG_INFO("ssl_enabled: {}", Flags::getInstance()->ssl_enabled);
    BUSLOG_INFO("out_with_https: {}", Flags::getInstance()->out_with_https);
    BUSLOG_INFO("log_file: {}", Flags::getInstance()->log_file);
    BUSLOG_INFO("log_dir: {}", Flags::getInstance()->log_dir);

    // init ssl
    SetLibProcessHttpsEnv();

    // init process
    bool ret = process::initialize(g_libprocess_server_name);
    if (!ret) {
        BUSLOG_ERROR("libprocess server init failed.");
        return -1;
    }

    UnSetLibProcessHttpsEnv();

    // spawn server process
    apiServer = std::make_shared<ServerProcess>();
    process::spawn(apiServer.get());
    (void)wait(apiServer->self());

    return 0;
}
