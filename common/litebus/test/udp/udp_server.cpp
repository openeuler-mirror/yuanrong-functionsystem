
#include <iostream>

#include <thread>

#include <signal.h>

#include <memory>
#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include "actor/iomgr.hpp"
#include "udp/udpmgr.hpp"

using namespace litebus;
using namespace std;

int recvNum = 0;
std::shared_ptr<IOMgr> io = nullptr;
string localUrl = "";
string remoteUrl = "";

void msgHandle(std::unique_ptr<MessageBase> &&msg)
{
    if (msg->GetType() == MessageBase::Type::KEXIT) {
        cout << "server recv exit msg name" << msg->name << " , from: " << msg->from << " , to: " << msg->to << endl;
        return;
    }
    cout << "server recv msg, name: " << msg->name << " , from: " << msg->from << " , to: " << msg->to << endl;
    recvNum++;

    AID from("testserver", localUrl);
    AID to("testserver", remoteUrl);
    std::unique_ptr<MessageBase> message(new MessageBase());

    message->name = "testname";
    message->from = from;
    message->to = to;
    message->body = "testbody";
    message->signature = "signature-server-1";
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

    io.reset(new UDPMgr());
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
    message->signature = "signature-server-0";
    cout << "to send" << endl;
    io->Send(std::move(message));

    sleep(100);

    cout << "test server end" << endl;
}
