// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <gmock/gmock.h>

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <process/collect.hpp>
#include <process/count_down_latch.hpp>
#include <process/future.hpp>
#include <process/gmock.hpp>
#include <process/gtest.hpp>
#include <process/owned.hpp>
#include <process/process.hpp>
#include <process/protobuf.hpp>
#include <process/delay.hpp>
#include <stout/duration.hpp>
#include <stout/gtest.hpp>
#include <stout/hashset.hpp>
#include <stout/stopwatch.hpp>
#include <process/ssl/gtest.hpp>
#include <process/ssl/utilities.hpp>

namespace http = process::http;

using process::CountDownLatch;
using process::Future;
using process::MessageEvent;
using process::Owned;
using process::Process;
using process::ProcessBase;
using process::Promise;
using process::UPID;

using std::cout;
using std::endl;
using std::list;
using std::ostringstream;
using std::string;

string localip = "";
string localport = "";
string remoteip = "";
string remoteport = "";
string downgrage = "0";
UPID TOID;
// A process that emulates the 'server' side of a ping pong game.
// Note that the server links to any clients communicating to it.
class ServerProcess : public Process<ServerProcess> {
public:
    ServerProcess() : ProcessBase("testserver"), tmpRecvNum(0), recvNum(0), recvLen(0)
    {
    }
    virtual ~ServerProcess()
    {
    }

protected:
    virtual void initialize()
    {
        // TODO(bmahler): Move in the message when move support is added.
        install("ping", &ServerProcess::ping);
        install("end", &ServerProcess::end);
        install("shakeHands", &ServerProcess::shakeHands);

        route("/post", ::None(), [this](const process::http::Request &request) { return process::http::OK(); });

        string to = ("testserver@" + remoteip + ":" + remoteport);

        string body = "shakeHands";
        send(TOID, "shakeHands", body.c_str(), body.size());
    }

private:
    void shakeHands(const UPID &from, const string &body)
    {
        BUSLOG_INFO("recv shakeHands: {}", from);
        if (!links.contains(TOID)) {
            link(TOID);
            links.insert(TOID);
        }
        send(TOID, "shakeHands", body.c_str(), body.size());
    }
    void ping(const UPID &from, const string &body)
    {
        BUSLOG_INFO("recv shakeHands: {}", from);
        if (!links.contains(TOID)) {
            link(TOID);
            links.insert(TOID);
        }
        recvNum++;
        recvLen = recvLen + body.size();
        send(TOID, "ping", body.c_str(), body.size());
    }

    void end(const UPID &from, const string &body)
    {
        string url = body;
        UPID endPid(url);

        if (!links.contains(endPid)) {
            link(endPid);
            links.insert(endPid);
        }
        std::ostringstream out;
        out << recvNum << "/" << recvLen;
        string data = std::to_string(recvNum) + "/" + std::to_string(recvLen);

        BUSLOG_INFO("server from {}, data: {}", from, data);

        send(endPid, "end", data.c_str(), data.size());
    }

    hashset<UPID> links;
    uint64_t tmpRecvNum;
    uint64_t recvNum;
    uint64_t recvLen;
};

void kill_handler(int sig)
{
    std::cout << "***********kill_handler, exit!************" << endl;
    exit(0);
}
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
extern void set_passwd_for_decrypting_private_key(const char *passwd_key, size_t passwd_len);
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

    if (argc == 4) {
        localurl = argv[1];
        remoteurl = argv[2];
        downgrage = argv[3];

    } else if (argc == 3) {
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

    const char *sslSandBox = getenv("LITEBUS_SSL_SANDBOX");

    std::string keyPath = std::string(sslSandBox) + "moca_keys/MSP_File";
    std::string certPath = std::string(sslSandBox) + "moca_keys/MSP.pem.cer";
    std::string rootCertPath = std::string(sslSandBox) + "moca_keys/CA.pem.cer";
    std::string rootCertDirPath = std::string(sslSandBox) + "moca_keys/";
    std::string decryptPath = std::string(sslSandBox) + "moca_keys/ct/";
    BUSLOG_INFO("keyPath is {},certPath is {}, rootCertPath is {}, decryptPath is {}", keyPath, certPath, rootCertPath,
                decryptPath);
    os::setenv("LIBPROCESS_SSL_ENABLED", "true");
    os::setenv("LIBPROCESS_SSL_KEY_FILE", keyPath);
    os::setenv("LIBPROCESS_SSL_CERT_FILE", certPath);

    os::setenv("LIBPROCESS_SSL_REQUIRE_CERT", "true");
    os::setenv("LIBPROCESS_SSL_VERIFY_CERT", "true");
    os::setenv("LIBPROCESS_SSL_CA_DIR", rootCertDirPath);
    os::setenv("LIBPROCESS_SSL_CA_FILE", rootCertPath);

    os::setenv("LIBPROCESS_SSL_VERIFY_IPADD", "0");

    string aaa = "Msp-4102";
    set_passwd_for_decrypting_private_key(aaa.c_str(), aaa.length());

    os::setenv("LIBPROCESS_IP", localip);
    os::setenv("LIBPROCESS_PORT", localport);

    BUSLOG_INFO("process initializing");
    process::initialize("ServerProcess");

    BUSLOG_INFO("process initialized ok");

    ServerProcess *server = new ServerProcess();
    UPID serverUpid = spawn(server);
    BUSLOG_INFO("serverUpid: {}", serverUpid);
    sleep(100);

    process::finalize();

    return 0;
}
