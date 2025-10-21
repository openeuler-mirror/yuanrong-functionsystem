
#include <iostream>

#include <thread>

#include <signal.h>

#include <memory>
#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include "actor/iomgr.hpp"
#include "tcp/tcpmgr.hpp"
#include "httpd/http_iomgr.hpp"

using namespace litebus;
using namespace std;

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
    testServerPath = (char *)"./testTcpServer";
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
        BUSLOG_INFO("recv exit msg name {}, from: {}, to: {}", msg->name, std::string(msg->from),
                    std::string(msg->to));
        return;
    }
    BUSLOG_INFO("recv msg name {}, from: {}, to: {}", msg->name, std::string(msg->from), std::string(msg->to));
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

int main(int argc, char **argv)
{
    struct sigaction act;

    act.sa_handler = kill_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGTERM, &act, 0);
    sigaction(SIGALRM, &act, 0);
    signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        cout << "check arg,argc " << argc << ",argv[1]: " << argv[1] << endl;
        return 0;
    }

    localUrl = argv[1];
    remoteUrl = argv[2];
    char *localpEnv = getenv("LITEBUS_IP");
    if (localpEnv != nullptr) {
        localIP = std::string(localpEnv);
    }
    litebus::HttpIOMgr::EnableHttp();

    io.reset(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(msgHandle);
    bool ret = io->StartIOServer(localUrl, localUrl);

    cout << "start server succ: " << ret << endl;

    AID from("testserver", localUrl);
    AID to("testserver", remoteUrl);
    std::unique_ptr<MessageBase> message(new MessageBase());

    message->name = "testname";
    message->from = from;
    message->to = to;
    message->body = "testbody";
    message->signature = "test-signature-server";
    cout << "to send" << endl;
    io->Send(std::move(message));

    sleep(100);

    cout << "test server end" << endl;
}
