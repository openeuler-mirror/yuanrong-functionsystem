#include <process/process.hpp>
#include <process/protobuf.hpp>
#include <process/future.hpp>
#include <stout/json.hpp>
#include <stout/os.hpp>
#include <sys/syscall.h>

using process::UPID;
using process::network::inet::Address;

using namespace std;

string localip = "";
string localport = "";
string remoteip = "";
string remoteport = "";
string downgrage = "0";
UPID TOID;

class UdpServerProcess : public ProtobufProcess<UdpServerProcess> {
public:
    UdpServerProcess() : ProcessBase("testserver")
    {
    }

    ~UdpServerProcess()
    {
    }

    virtual void initialize()
    {
        BUSLOG_INFO("UdpServerProcess initialize");
        install("ping", &UdpServerProcess::ping);
        string body = "shakeHands";
        sendUdp(TOID, "ping", body.c_str(), body.size());
    }

    void exited(const UPID &t)
    {
        BUSLOG_INFO("UdpClientProcess out {}", t);
        links.erase(t);
    }
    void ping(const UPID &from, const string &body)
    {
        BUSLOG_INFO("recv ping, from: {}", from);
        sendUdp(TOID, "ping", body.c_str(), body.size());
    }

    hashset<UPID> links;
};

static const std::string URL_PROTOCOL_IP_SEPARATOR = "://";

string getip(string url)
{
    std::string ip;
    size_t index1 = url.find(URL_PROTOCOL_IP_SEPARATOR);
    if (index1 == std::string::npos) {
        index1 = 0;
    } else {
        index1 = index1 + URL_PROTOCOL_IP_SEPARATOR.length();
    }

    size_t index2 = url.rfind(':');

    ip = url.substr(index1, index2 - index1);

    return ip;
}

string getport(string url)
{
    std::string port;

    size_t index1 = url.rfind(':');

    port = url.substr(index1 + 1);

    return port;
}
void kill_handler(int sig)
{
    std::cout << "***********kill_handler, exit!************" << endl;
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

    string localurl;
    string remoteurl;

    if (argc == 3) {
        localurl = argv[1];
        remoteurl = argv[2];
    } else {
        BUSLOG_INFO("check arg, argc: {}, argv[1]: {}", argc, argv[1]);
        return 0;
    }

    localip = getip(localurl);
    localport = getport(localurl);
    remoteip = getip(remoteurl);
    remoteport = getport(remoteurl);

    TOID = ("testserver@" + remoteip + ":" + remoteport);
    os::setenv("LIBPROCESS_IP", localip);
    os::setenv("LIBPROCESS_PORT", localport);

    os::setenv("LIBPROCESS_PORT", localport);    // "55155"
    os::setenv("LIBPROCESS_UC_UDP_ENABLED", "1");
    os::setenv("LIBPROCESS_UC_UDP_PORT", localport);
    os::unsetenv("LIBPROCESS_MSG_PORT_ENABLED");
    os::unsetenv("LIBPROCESS_MSG_PORT");

    BUSLOG_INFO("process initializing");
    process::initialize("UdpServerProcess");
    BUSLOG_INFO("process initialized ok");
    UdpServerProcess server;
    UPID udpServerUpid = spawn(&server);

    sleep(100);
    process::finalize();

    return 0;
}
