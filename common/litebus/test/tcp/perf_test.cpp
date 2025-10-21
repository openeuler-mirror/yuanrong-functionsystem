#include <signal.h>
#include <time.h>

#include <memory>
#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include <actor/iomgr.hpp>
#include <iostream>
#include <tcp/tcpmgr.hpp>
#include <thread>

using namespace litebus;
using namespace std;

uint64_t recvNum = 0;
std::shared_ptr<IOMgr> io = nullptr;
string localUrl = "";
string remoteUrl = "";
int m_msgSize = 1024;
int m_batch = 0;
int m_count = 500000;

bool m_isServer = true;
string m_msgData;

#define USECS_IN_SEC 1000000
#define NSECS_IN_USEC 1000

static inline uint64_t get_time_us(void)
{
    uint64_t retval = 0;
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    retval = ts.tv_sec * 1000000;    // USECS_IN_SEC *NSECS_IN_USEC;
    retval += ts.tv_nsec / 1000;
    return retval;
}

void SendMsg(AID from, AID to, bool remoteLink)
{
    std::unique_ptr<MessageBase> message(new MessageBase());
    message->name = "testname";
    message->SetFrom(from);
    message->SetTo(to);
    if (m_isServer) {
        message->body = "ok";
    } else {
        message->body = m_msgData;
    }

    io->Send(std::move(message), remoteLink);
}

void msgHandle(std::unique_ptr<MessageBase> &&msg)
{
    if (msg->GetType() == MessageBase::Type::KEXIT) {
        BUSLOG_INFO("server recv exit msg, name {}, from: {}, to: {}", msg->name, std::string(msg->from),
                    std::string(msg->to));
        return;
    }

    SendMsg(msg->to, msg->from, false);

    // cout << "server recv msg, name: " << msg->name << " , from: " << msg->from
    //     << " , to: " << msg->to << endl;
    recvNum++;
}

void kill_handler(int sig)
{
    BUSLOG_ERROR("***********kill_handler, exit!************");
    io->Finish();
    exit(0);
}

// arg: localurl,remoteurl,size,batch,count
int main(int argc, char **argv)
{
    struct sigaction act;

    act.sa_handler = kill_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGTERM, &act, 0);
    sigaction(SIGALRM, &act, 0);
    signal(SIGPIPE, SIG_IGN);

    if (2 == argc) {
        localUrl = argv[1];
    } else if (6 == argc) {
        localUrl = argv[1];
        remoteUrl = argv[2];
        m_msgSize = atoi(argv[3]);
        string msgdata(m_msgSize, 'A');
        m_msgData = msgdata;
        m_batch = atoi(argv[4]);
        m_count = atoi(argv[5]);
        m_isServer = false;
    } else {
        BUSLOG_INFO("check arg, argc {}, argv[1]: {}", argc, argv[1]);
        return 0;
    }

    io.reset(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(msgHandle);
    bool ret = io->StartIOServer(localUrl, localUrl);

    BUSLOG_INFO("start server succ: {}", ret);

    uint64_t tmprecvnum1 = 0;
    uint64_t tmprecvnum2 = 0;
    uint64_t tps = 0;
    while (true) {
        sleep(1);
        tmprecvnum2 = recvNum;
        tps = tmprecvnum2 - tmprecvnum1;
        if (m_isServer) {
            BUSLOG_INFO("server] tps: {}", tps);
        } else {
            BUSLOG_INFO("client] m_msgSize: {}, m_batch: {}, tps: {}", m_msgSize, m_batch, tps);
        }
        tmprecvnum1 = tmprecvnum2;
        if (0 == tps) {
            if (!m_isServer) {
                int i = 0;
                AID from("testserver", localUrl);
                AID to("testserver", remoteUrl);
                for (i = 0; i < m_batch; i++) {
                    SendMsg(from, to, false);
                }
            }
        }
    }

    sleep(100);
    BUSLOG_INFO("test server end");
}
