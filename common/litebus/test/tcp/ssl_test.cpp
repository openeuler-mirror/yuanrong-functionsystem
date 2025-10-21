/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <atomic>
#include <iostream>
#include <string>

#include <thread>

#include "openssl/err.h"
#include "openssl/rand.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

#include <gtest/gtest.h>
#include <signal.h>

#include "utils/os_utils.hpp"
#include "litebus.hpp"
#include "ssl/openssl_wrapper.hpp"
#include "ssl/ssl_socket.hpp"
#include "securec.h"

#include "actor/actorapp.hpp"
#include "actor/iomgr.hpp"
#include "async/async.hpp"
#include "tcp/tcpmgr.hpp"
#include "ssl/ssl_env.hpp"

using namespace std;
constexpr auto PASSWDLEN = 512;

namespace litebus {
namespace openssl {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
extern int VerifyCallback(int ret, X509_STORE_CTX *store);
extern CRYPTO_dynlock_value *DynCreateFun(const char *file, int line);
extern void DynLockFun(int mode, CRYPTO_dynlock_value *value, const char *file, int line);
extern void DynKillLockFun(CRYPTO_dynlock_value *value, const char *file, int line);
#endif
bool SslInitInternal();
bool SetVerifyContextFromPem(SSL_ENVS *sslEnvs, SSL_CTX *sslCtx);
}    // namespace openssl
}    // namespace litebus

namespace litebus {
int recvSslNum = 0;
int exitSslMsg = 0;
TCPMgr *m_sslIo = nullptr;
std::atomic<int> m_sendSslNum(0);
std::string m_recvSslBody;
string m_localIP = "127.0.0.1";
bool m_notRemote = false;

string localurl1;
string localurl2;
string remoteurl1;
string remoteurl2;

void sslMsgHandle(std::unique_ptr<MessageBase> &&msg)
{
    if (msg->GetType() == MessageBase::Type::KEXIT) {
        BUSLOG_INFO("SSLTest]recv exit msg name {}, from: {}, to: {}", msg->name, std::string(msg->from),
                    std::string(msg->to));
        exitSslMsg++;
        return;
    }
    m_recvSslBody = msg->body;
    BUSLOG_INFO("SSLTest]recv msg name {}, from: {}, to: {}", msg->name, std::string(msg->from), std::string(msg->to));
    recvSslNum++;
}

class SSLTest : public ::testing::Test {
protected:
    char *args1[4];
    char *args2[4];
    char *testServerPath;
    pid_t pid1;
    pid_t pid2;

    pid_t pids[100];

    void SetUp()
    {
        char *localpEnv = getenv("LITEBUS_IP");
        if (localpEnv != nullptr) {
            m_localIP = std::string(localpEnv);
        }

        char *locaNotRemoteEnv = getenv("LITEBUS_SEND_ON_REMOTE");
        if (locaNotRemoteEnv != nullptr) {
            m_notRemote = (std::string(locaNotRemoteEnv) == "true") ? true : false;
        }

        BUSLOG_INFO("start");
        pid1 = 0;
        pid2 = 0;

        memset_s(pids, 100 * sizeof(pid_t), 0, 100 * sizeof(pid_t));
        recvSslNum = 0;
        exitSslMsg = 0;
        m_sendSslNum = 0;
        testServerPath = (char *)"./testSslServer";
        args1[0] = (char *)testServerPath;
        // local url
        localurl1 = string("tcp://" + m_localIP + ":2224");
        args1[1] = (char *)localurl1.data();
        // remote url
        remoteurl1 = string("tcp://" + m_localIP + ":2225");
        args1[2] = (char *)remoteurl1.data();
        args1[3] = (char *)nullptr;

        args2[0] = (char *)testServerPath;
        localurl2 = string("tcp://" + m_localIP + ":2225");
        args2[1] = (char *)localurl2.data();
        remoteurl2 = string("tcp://" + m_localIP + ":2223");
        args2[2] = (char *)remoteurl2.data();
        args2[3] = (char *)nullptr;

        const char *sslSandBox = getenv("LITEBUS_SSL_SANDBOX");
        ASSERT_TRUE(sslSandBox != nullptr);
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
        environment["LITEBUS_SSL_LOAD_FROM_FILE"] = "1";

        FetchSSLConfigFromMap(environment);
        auto pKey = std::string("Msp-4102");
        litebus::SetPasswdForDecryptingPrivateKey(pKey.c_str(), pKey.length());
        litebus::openssl::SslInitInternal();

        m_sslIo = new TCPMgr();
        m_sslIo->Init();
        m_sslIo->RegisterMsgHandle(sslMsgHandle);
        bool ret = m_sslIo->StartIOServer("tcp://" + m_localIP + ":2223", "tcp://" + m_localIP + ":2223");
        BUSLOG_INFO("start server ret: {}", ret);
    }
    void TearDown()
    {
        BUSLOG_INFO("finish");
        shutdownTcpServer(pid1);
        shutdownTcpServer(pid2);
        pid1 = 0;
        pid2 = 0;
        int i = 0;
        for (i = 0; i < 100; i++) {
            shutdownTcpServer(pids[i]);
            pids[i] = 0;
        }
        recvSslNum = 0;
        exitSslMsg = 0;
        m_sendSslNum = 0;
        if (m_sslIo) {
            m_sslIo->Finish();
            delete m_sslIo;
            m_sslIo = nullptr;
        }
        litebus::openssl::SslFinalize();
    }
    bool CheckRecvNum(int expectedRecvNum, int _timeout);
    bool ChecKEXITNum(int expectedExitNum, int _timeout);
    pid_t startTcpServer(char **args);
    void shutdownTcpServer(pid_t pid);
    void KillTcpServer(pid_t pid);

    void Link(string &_localUrl, string &_remoteUrl);
    void Reconnect(string &_localUrl, string &_remoteUrl);
    void Unlink(string &_remoteUrl);

public:
    static void SendMsg(string &_localUrl, string &_remoteUrl, int msgsize, bool remoteLink = false, string body = "");
    static void GenPemCert(EVP_PKEY *pkey, X509 *cert, EVP_PKEY *caPkey = nullptr, X509 *caCert = nullptr,
                           long before = 0);
};

// listening local url and sending msg to remote url,if start succ.
pid_t SSLTest::startTcpServer(char **args)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (execv(args[0], args) == -1) {
            BUSLOG_INFO("execve failed, errno: {}, args: {}, args[0]: {}", errno, *args, args[0]);
        }
        return -1;
    } else {
        return pid;
    }
}
void SSLTest::shutdownTcpServer(pid_t pid)
{
    if (pid > 1) {
        kill(pid, SIGALRM);
        int status;
        waitpid(pid, &status, 0);
        BUSLOG_INFO("status = {}", status);
    }
}

void SSLTest::KillTcpServer(pid_t pid)
{
    if (pid > 1) {
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
        BUSLOG_INFO("status = {}", status);
    }
}

void SSLTest::SendMsg(string &_localUrl, string &_remoteUrl, int msgsize, bool remoteLink, string body)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);

    std::unique_ptr<MessageBase> message(new MessageBase());
    string data(msgsize, 'A');
    message->name = "testname";
    message->from = from;
    message->to = to;
    message->body = data;
    if (body != "") {
        message->body = body;
    }

    // cout << "to send"
    //    << endl;
    if (m_notRemote) {
        m_sslIo->Send(std::move(message), remoteLink, true);
    } else {
        m_sslIo->Send(std::move(message), remoteLink);
    }
}

void SSLTest::Link(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_sslIo->Link(from, to);
}

void SSLTest::Reconnect(string &_localUrl, string &_remoteUrl)
{
    AID from("testserver", _localUrl);
    AID to("testserver", _remoteUrl);
    m_sslIo->Reconnect(from, to);
}

void SSLTest::Unlink(string &_remoteUrl)
{
    AID to("testserver", _remoteUrl);
    m_sslIo->UnLink(to);
}
//_timeout: s
bool SSLTest::CheckRecvNum(int expectedRecvNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;    // us
    int usleepCount = 100000;                // 100ms

    while (timeout) {
        usleep(usleepCount);
        if (recvSslNum >= expectedRecvNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }
    return false;
}

bool SSLTest::ChecKEXITNum(int expectedExitNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;
    int usleepCount = 100000;

    while (timeout) {
        usleep(usleepCount);
        if (exitSslMsg >= expectedExitNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }

    return false;
}

void SSLTest::GenPemCert(EVP_PKEY *pkey, X509 *cert, EVP_PKEY *caPkey, X509 *caCert, long before)
{
    // generate a private key
    BIGNUM *bne = BN_new();
    BN_set_word(bne, RSA_F4);
    RSA *r = RSA_new();
    RSA_generate_key_ex(r, 2048, bne, nullptr);
    EVP_PKEY_assign_RSA(pkey, r);

    // generating a certificate
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), before);
    X509_gmtime_adj(X509_get_notAfter(cert), 31536000L);
    X509_set_pubkey(cert, pkey);

    // setting certificate information
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"CN", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char *)"My Company", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"My Root CA", -1, -1, 0);
    X509_set_issuer_name(cert, caCert != nullptr ? X509_get_subject_name(caCert) : name);

    // add subject alt name in extension
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:ServiceDNS");
    X509_add_ext(cert, ext, -1);

    // signature certificate
    X509_sign(cert, caPkey != nullptr ? caPkey : pkey, EVP_sha256());

    // releasing memory
    X509_EXTENSION_free(ext);
    name = nullptr;
    r = nullptr;
    BN_free(bne);
}

TEST_F(SSLTest, StartServerFail)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();

    bool ret = io->StartIOServer("tcp://0:2223", "tcp://0:2223");
    BUSLOG_INFO("ret: {}", ret);
    ASSERT_FALSE(ret);

    ret = io->StartIOServer("tcp://" + m_localIP + ":2223", "tcp://" + m_localIP + ":2223");
    BUSLOG_INFO("ret: {}", ret);
    io->Finish();
    ASSERT_FALSE(ret);
}

TEST_F(SSLTest, StartServer2)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(sslMsgHandle);
    bool ret = io->StartIOServer("tcp://" + m_localIP + ":2223", "tcp://" + m_localIP + ":2223");
    ASSERT_FALSE(ret);
    ret = io->StartIOServer("tcp://" + m_localIP + ":2224", "tcp://" + m_localIP + ":2224");
    BUSLOG_INFO("ret: {}", ret);
    io->Finish();
    ASSERT_TRUE(ret);
}

// server -> client -> server -> client
TEST_F(SSLTest, send1Msg)
{
    recvSslNum = 0;

    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 1005);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// batch send 10 msgs
TEST_F(SSLTest, send10Msg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int sendnum = 10;
    while (sendnum--) {
        SendMsg(from, to, 100);
    }

    ret = CheckRecvNum(11, 10);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// batch send 10 msgs
TEST_F(SSLTest, send10Msg2)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int sendnum = 10;
    while (sendnum--) {
        SendMsg(from, to, 8192);
    }

    ret = CheckRecvNum(11, 10);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(SSLTest, SendMsgCloseOnExec)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);

    SendMsg(from, to, 100, false, "CloseOnExec");

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    BUSLOG_INFO("************ ", m_recvSslBody);
    std::string recvBody = m_recvSslBody;

    pid2 = std::stoul(recvBody.substr(4));
    KillTcpServer(pid1);

    pid1 = startTcpServer(args2);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);
    SendMsg(from, to, 100);
    ret = CheckRecvNum(4, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    shutdownTcpServer(pid2);
    pid2 = 0;
    pid1 = 0;
}

// server -> client -> server -> client
TEST_F(SSLTest, sendMsgByRemoteLink)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100, true);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// link and send msg
TEST_F(SSLTest, link_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    shutdownTcpServer(pid1);
    pid1 = 0;
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);
}

// To test link is exist, Test steps: send,link,send;
TEST_F(SSLTest, link2_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// linker,Test steps: send,link,link,send
TEST_F(SSLTest, link3_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    SendMsg(from, to, 100);
    Link(from, to);
    string from1 = "tcp://" + m_localIP + ":2222";
    Link(from1, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: reconnect,send
TEST_F(SSLTest, reconnect_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";

    Reconnect(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,send,reconnect,send
TEST_F(SSLTest, send_reconnect2_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Reconnect(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,send,shutdown server,send ,start server,reconnect,send
TEST_F(SSLTest, reconnect3_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    shutdownTcpServer(pid1);
    SendMsg(from, to, 100);
    pid1 = startTcpServer(args2);
    ret = CheckRecvNum(3, 15);
    ASSERT_TRUE(ret);
    Reconnect(from, to);

    SendMsg(from, to, 100);

    ret = CheckRecvNum(4, 15);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,unlink,send
TEST_F(SSLTest, unlink_sendMsg)
{
    recvSslNum = 0;
    exitSslMsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    Unlink(to);
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);
    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,link,send,unlink,send
TEST_F(SSLTest, unlink2_sendMsg)
{
    recvSslNum = 0;
    exitSslMsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    string from2 = "tcp://" + m_localIP + ":2222";
    Link(from2, to);
    SendMsg(from, to, 100);
    ret = CheckRecvNum(2, 5);

    Unlink(to);
    ret = ChecKEXITNum(2, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps: link,send,shutdown server,send,start server,unlink,send
TEST_F(SSLTest, unlink3_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    Link(from, to);
    SendMsg(from, to, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    shutdownTcpServer(pid1);
    SendMsg(from, to, 100);
    pid1 = startTcpServer(args2);
    ret = CheckRecvNum(3, 5);
    ASSERT_TRUE(ret);
    Unlink(to);
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(from, to, 100);

    ret = CheckRecvNum(4, 5);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps:
TEST_F(SSLTest, unlink4_sendMsg)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    AID from("testserver", "tcp://" + m_localIP + ":2223");
    AID to("testserver", "tcp://" + m_localIP + ":2225");
    m_sslIo->Link(from, to);

    AID to2("testserver2", "tcp://" + m_localIP + ":2225");
    m_sslIo->Link(from, to2);

    string fromurl = "tcp://" + m_localIP + ":2223";
    string tourl = "tcp://" + m_localIP + ":2225";

    SendMsg(fromurl, tourl, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    Unlink(tourl);
    ret = ChecKEXITNum(2, 5);
    ASSERT_TRUE(ret);

    shutdownTcpServer(pid1);
    pid1 = 0;
}

// Test steps:
TEST_F(SSLTest, unlink5)
{
    recvSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);

    AID from("testserver", "tcp://" + m_localIP + ":2223");
    AID to("testserver", "tcp://" + m_localIP + ":2225");
    string fromurl = "tcp://" + m_localIP + ":2223";
    string tourl = "tcp://" + m_localIP + ":2225";

    Unlink(tourl);
    sleep(1);
    EXPECT_EQ(exitSslMsg, 0);

    m_sslIo->Link(from, to);

    AID to2("testserver2", "tcp://" + m_localIP + ":2228");
    string tourl2 = "tcp://" + m_localIP + ":2228";
    m_sslIo->Link(from, to2);
    ret = ChecKEXITNum(1, 5);
    ASSERT_TRUE(ret);

    SendMsg(fromurl, tourl, 100);

    ret = CheckRecvNum(2, 5);
    ASSERT_TRUE(ret);
    Unlink(tourl);

    shutdownTcpServer(pid1);
    pid1 = 0;
}

struct SendMsgCtx {
    int sendNum;
    int sendSize;
    string from;
    string to;
};

void *SendSslThreadFunc(void *arg)
{
    if (!arg) {
        return nullptr;
    }
    SendMsgCtx *sendctx = (SendMsgCtx *)arg;

    int i = 0;
    for (i = 0; i < sendctx->sendNum; i++) {
        m_sendSslNum++;
        SSLTest::SendMsg(sendctx->from, sendctx->to, sendctx->sendSize);
    }
    delete sendctx;
    return nullptr;
}

// batch send; 1 thread,batch send 100 msgs
TEST_F(SSLTest, sendMsg100)
{
    recvSslNum = 0;
    exitSslMsg = 0;
    m_sendSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 1;
    pthread_t threadIds[threadNum];
    int sendsize = 100;
    int batch = 100;
    int i = 0;

    for (i = 0; i < threadNum; i++) {
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendSslThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// batch send big packages(size=1M)
TEST_F(SSLTest, sendMsg10_1M)
{
    recvSslNum = 0;
    exitSslMsg = 0;
    m_sendSslNum = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 1;
    pthread_t threadIds[threadNum];
    int sendsize = 1024 * 1024;
    int batch = 10;
    int i = 0;

    for (i = 0; i < threadNum; i++) {
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendSslThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// 100 threads send
TEST_F(SSLTest, SendConcurrently_100threads)
{
    recvSslNum = 0;
    exitSslMsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 100;
    pthread_t threadIds[threadNum];
    int sendsize = 2;
    int batch = 10;
    int i = 0;
    Link(from, to);
    for (i = 0; i < threadNum; i++) {
        sendsize = sendsize << 1;
        if (sendsize > 1048576) {
            sendsize = 2;
        }
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendSslThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

TEST_F(SSLTest, SendConcurrently2_100threads)
{
    recvSslNum = 0;
    exitSslMsg = 0;
    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":2225";
    int threadNum = 100;
    pthread_t threadIds[threadNum];
    int sendsize = 2;
    int batch = 10;
    int i = 0;

    for (i = 0; i < threadNum; i++) {
        sendsize = sendsize << 1;
        if (sendsize > 1048576) {
            sendsize = 2;
        }
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to;
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendSslThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
    }
    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);

    Unlink(to);
    shutdownTcpServer(pid1);
    pid1 = 0;
}

// A->B1,B2...B100 -> C-> A
TEST_F(SSLTest, sendMsg_100Servers)
{
    int serverNum = 100;
    recvSslNum = 0;
    exitSslMsg = 0;
    int i = 0;
    int port = 3100;

    pid1 = startTcpServer(args2);
    bool ret = CheckRecvNum(1, 5);
    BUSLOG_INFO("***************sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    for (i = 0; i < serverNum; i++) {
        string localurl = "tcp://" + m_localIP + ":" + std::to_string(port);
        args1[1] = (char *)localurl.data();

        pids[i] = startTcpServer(args1);
        port++;
    }

    ret = CheckRecvNum(serverNum + 1, 15);
    BUSLOG_INFO("***************sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);
    string from = "tcp://" + m_localIP + ":2223";
    string to = "tcp://" + m_localIP + ":";
    int threadNum = serverNum;
    pthread_t threadIds[threadNum];
    int sendsize = 2;
    int batch = 10;
    i = 0;
    port = 3100;
    for (i = 0; i < threadNum; i++) {
        sendsize = sendsize << 1;
        if (sendsize > 1048576) {
            sendsize = 2;
        }
        SendMsgCtx *sendctx = new SendMsgCtx();
        sendctx->from = from;
        sendctx->to = to + std::to_string(port);
        sendctx->sendSize = sendsize;
        sendctx->sendNum = batch;
        if (pthread_create(&threadIds[i], nullptr, SendSslThreadFunc, (void *)sendctx) != 0) {
            BUSLOG_ERROR("pthread_create failed");
            ASSERT_TRUE(false);
        }
        port++;
    }

    void *threadResult;
    for (i = 0; i < threadNum; i++) {
        int joinret = pthread_join(threadIds[i], &threadResult);
        if (0 != joinret) {
            BUSLOG_INFO("pthread_join loopThread failed,i: {}", i);
        } else {
            BUSLOG_INFO("pthread_join loopThread succ,i: {}", i);
        }
    }

    ret = CheckRecvNum(batch * threadNum + serverNum + 1, 20);
    BUSLOG_INFO("sendNum: {}, recvSslNum: {}", m_sendSslNum, recvSslNum);
    ASSERT_TRUE(ret);
    port = 3100;
    for (i = 0; i < serverNum; i++) {
        string unlinkto = to + std::to_string(port);
        Unlink(unlinkto);
        shutdownTcpServer(pids[i]);
        pids[i] = 0;
        port++;
    }

    shutdownTcpServer(pid1);
    pid1 = 0;
}

void TestSslLinkerCallBack(const std::string &from, const std::string &to)
{
    BUSLOG_INFO("from: {}, to: {}", from, to);
}

TEST_F(SSLTest, LinkMgr)
{
    LinkMgr *linkMgr = new LinkMgr();
    Connection *conn = new Connection();
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = false;
    conn->from = "tcp://" + m_localIP + ":1111";
    conn->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn);
    const AID from("testserver", "tcp://" + m_localIP + ":1111");
    const AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd, from, to, TestSslLinkerCallBack);

    conn = new Connection();
    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = true;
    conn->from = "tcp://" + m_localIP + ":1113";
    conn->to = "tcp://" + m_localIP + ":1114";

    linkMgr->AddLink(conn);
    std::string toUrl = "tcp://" + m_localIP + ":1114";
    conn = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, LinkMgr2)
{
    LinkMgr *linkMgr = new LinkMgr();
    Connection *conn = new Connection();
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = false;
    conn->from = "tcp://" + m_localIP + ":1111";
    conn->to = "tcp://" + m_localIP + ":1112";

    linkMgr->AddLink(conn);
    AID from("testserver", "tcp://" + m_localIP + ":1111");
    AID to("testserver", "tcp://" + m_localIP + ":1112");

    linkMgr->AddLinker(fd, from, to, TestSslLinkerCallBack);

    LinkerInfo *linker = linkMgr->FindLinker(fd, from, to);
    ASSERT_TRUE(linker);
    linkMgr->DeleteAllLinker();
    linker = linkMgr->FindLinker(fd, from, to);
    ASSERT_FALSE(linker);

    conn = new Connection();
    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        BUSLOG_INFO("create socket fail: {}", errno);
        return;
    }
    conn->fd = fd;
    conn->isRemote = true;
    conn->from = "tcp://" + m_localIP + ":1113";
    conn->to = "tcp://" + m_localIP + ":1114";

    linkMgr->AddLink(conn);
    std::string toUrl = "tcp://" + m_localIP + ":1114";
    conn = linkMgr->FindLink(toUrl, true);
    ASSERT_TRUE(conn);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, EvbufMgr)
{
    EvbufMgr *evbufmgr = new EvbufMgr();
    ASSERT_TRUE(evbufmgr);
    delete evbufmgr;
    evbufmgr = nullptr;
}

TEST_F(SSLTest, TCPMgr)
{
    TCPMgr *tcpmgr = new TCPMgr();
    ASSERT_TRUE(tcpmgr);
    delete tcpmgr;
    tcpmgr = nullptr;
}

TEST_F(SSLTest, EvLoop)
{
    EvLoop *evLoop = new EvLoop();
    ASSERT_TRUE(evLoop);
    int ret = evLoop->AddFdEvent(-1, 1, nullptr, nullptr);
    ASSERT_FALSE(ret == BUS_OK);
    evLoop->AddFuncToEvLoop([ret] {
        // will not perform
        ASSERT_TRUE(ret);
    });
    ret = evLoop->Init("testTcpEvloop");
    ASSERT_TRUE(ret);

    void *threadResult;

    evLoop->StopEventLoop();

    pthread_join(evLoop->loopThread, &threadResult);

    close(evLoop->queueEventfd);
    evLoop->queueEventfd = -1;
    ret = false;
    evLoop->AddFuncToEvLoop([ret] {
        // will not perform
        ASSERT_TRUE(ret);
    });
    delete evLoop;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
TEST_F(SSLTest, VerifyCallback)
{
    int ret = 0;
    X509_STORE_CTX *store = new X509_STORE_CTX();
    ret = openssl::VerifyCallback(ret, store);
    ASSERT_TRUE(ret == 0);
    delete store;
    store = nullptr;
}

TEST_F(SSLTest, SslSend)
{
    int ret = 0;
    SSL *ssl = new SSL();
    char *buf = new char;
    uint32_t len = 1;
    ret = SSLSocketOperate::SslSend(ssl, buf, len);
    ASSERT_TRUE(ret == -1);
    delete ssl;
    ssl = nullptr;
    delete buf;
    buf = nullptr;
}
#endif

TEST_F(SSLTest, ClearPasswdForDecryptingPrivateKeyTest)
{
    {
        litebus::os::SetEnv("LITEBUS_SSL_ENABLED", "false", false);
        litebus::os::SetEnv("LITEBUS_SSL_VERIFY_CERT", "false", false);
        litebus::os::SetEnv("LITEBUS_SSL_REQUIRE_CERT", "false", false);
        litebus::os::SetEnv("LITEBUS_SSL_CA_DIR", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_CA_FILE", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_CERT_FILE", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_KEY_FILE", "", false);

        FetchSSLConfigFromEnvCA();

        litebus::os::UnSetEnv("LITEBUS_SSL_ENABLED");
        litebus::os::UnSetEnv("LITEBUS_SSL_VERIFY_CERT");
        litebus::os::UnSetEnv("LITEBUS_SSL_REQUIRE_CERT");
        litebus::os::UnSetEnv("LITEBUS_SSL_CA_DIR");
        litebus::os::UnSetEnv("LITEBUS_SSL_CA_FILE");
        litebus::os::UnSetEnv("LITEBUS_SSL_CERT_FILE");
        litebus::os::UnSetEnv("LITEBUS_SSL_KEY_FILE");
    }

    {
        litebus::os::SetEnv("LITEBUS_SSL_DECRYPT_DIR", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_DECRYPT_ROOT_FILE", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_DECRYPT_COMMON_FILE", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_DECRYPT_KEY_FILE", "", false);
        litebus::os::SetEnv("LITEBUS_SSL_DECRYPT_TYPE", "", false);

        FetchSSLConfigFromEnvDecrypt();

        litebus::os::UnSetEnv("LITEBUS_SSL_DECRYPT_DIR");
        litebus::os::UnSetEnv("LITEBUS_SSL_DECRYPT_ROOT_FILE");
        litebus::os::UnSetEnv("LITEBUS_SSL_DECRYPT_COMMON_FILE");
        litebus::os::UnSetEnv("LITEBUS_SSL_DECRYPT_KEY_FILE");
        litebus::os::UnSetEnv("LITEBUS_SSL_DECRYPT_TYPE");
    }

    {
        SetSSLEnvsDecrypt("LITEBUS_SSL_FETCH_FROM_ENV", "true");
    }

    {
        auto ret = ClearPasswdForDecryptingPrivateKey();
        EXPECT_EQ(ret, 0);
    }
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
TEST_F(SSLTest, DynTest)
{
    CRYPTO_dynlock_value *value = openssl::DynCreateFun(nullptr, 0);
    EXPECT_TRUE(value != nullptr);

    openssl::DynLockFun(1, value, nullptr, 0);
    openssl::DynLockFun(0, value, nullptr, 0);

    openssl::DynKillLockFun(value, nullptr, 0);
}
#endif

TEST_F(SSLTest, ExactDeleteLinkTest)
{
    LinkMgr *linkMgr = new LinkMgr();
    EXPECT_TRUE(linkMgr != nullptr);

    string to("tcp://" + m_localIP + ":1112");
    linkMgr->ExactDeleteLink(to, false);
    Connection *connection = linkMgr->FindLink(to, false, true);
    ASSERT_EQ(connection, nullptr);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, LitebusSetPasswdForDecryptingPrivateKeyC01)
{
    LinkMgr *linkMgr = new LinkMgr();
    EXPECT_TRUE(linkMgr != nullptr);

    linkMgr->ExactDeleteLink("tcp://" + m_localIP + ":1112", false);
    char tmp = 'x';
    char *passwdPtr = &tmp;
    size_t passwdLen = 520;
    LitebusSetPasswdForDecryptingPrivateKeyC(passwdPtr, passwdLen);
    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, LitebusSetPasswdForDecryptingPrivateKeyC02)
{
    LinkMgr *linkMgr = new LinkMgr();
    EXPECT_TRUE(linkMgr != nullptr);

    linkMgr->ExactDeleteLink("tcp://" + m_localIP + ":1112", false);
    char *passwdPtr = new char[516];
    size_t passwdLen = 1;
    LitebusSetPasswdForDecryptingPrivateKeyC(passwdPtr, passwdLen);
    delete linkMgr;
    linkMgr = nullptr;
    delete[] passwdPtr;
    passwdPtr = nullptr;
}

TEST_F(SSLTest, DecryptingPrivateKeyTest)
{
    SetPasswdForDecryptingPrivateKey(nullptr, 0);

    auto res = GetPasswdForDecryptingPrivateKey(nullptr, 0);
    EXPECT_EQ(res, -1);
}

TEST_F(SSLTest, DecryptingPrivateKeyTest1)
{
    char out[PASSWDLEN - 1] = { 0 };
    SetPasswdForDecryptingPrivateKey(out, PASSWDLEN - 1);

    auto res = GetPasswdForDecryptingPrivateKey(out, PASSWDLEN + 1);
    EXPECT_EQ(res, 0);
}

TEST_F(SSLTest, RefreshMetricsTest)
{
    LinkMgr *linkMgr = new LinkMgr();
    EXPECT_TRUE(linkMgr != nullptr);

    linkMgr->RefreshMetrics();

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, FindMaxLinkTest)
{
    LinkMgr *linkMgr = new LinkMgr();
    EXPECT_TRUE(linkMgr != nullptr);

    auto res = linkMgr->FindMaxLink();
    EXPECT_TRUE(res == nullptr);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, FindFastLinkTest)
{
    LinkMgr *linkMgr = new LinkMgr();
    EXPECT_TRUE(linkMgr != nullptr);

    auto res = linkMgr->FindFastLink();
    EXPECT_TRUE(res == nullptr);

    delete linkMgr;
    linkMgr = nullptr;
}

TEST_F(SSLTest, SslSendFail)
{
    SSLSocketOperate sslSocket;
    uint32_t len = -1;
    auto ret = sslSocket.SslSend(nullptr,  nullptr, len);
    EXPECT_EQ(ret, -1);
}

TEST_F(SSLTest, SslSendMsgFail)
{
    SSLSocketOperate sslSocket;
    uint32_t len = -1;
    struct iovec sendIov[1];
    sendIov[1].iov_base = nullptr;
    sendIov[1].iov_len = 0;

    struct msghdr sendMsg = { 0 };
    sendMsg.msg_iov = sendIov;
    sendMsg.msg_iovlen = 1;

    SSL_CTX* ssl_ctx = SSL_CTX_new(TLS_client_method());
    auto ssl = SSL_new(ssl_ctx);
    Connection conn;
    conn.ssl = ssl;
    auto ret = sslSocket.Sendmsg(&conn, &sendMsg, len);
    EXPECT_EQ(ret, -1);
}

TEST_F(SSLTest, LitebusSetSSLPemKeyEnvsCTest)
{
    EXPECT_EQ(LitebusSetSSLPemKeyEnvsC(nullptr), -1);
    EVP_PKEY *pkey = EVP_PKEY_new();
    EXPECT_EQ(LitebusSetSSLPemKeyEnvsC(pkey), 0);
}

TEST_F(SSLTest, LitebusSetSSLPemCertEnvsCTest)
{
    EXPECT_EQ(LitebusSetSSLPemCertEnvsC(nullptr), -1);
    X509 *x509 = X509_new();
    EXPECT_EQ(LitebusSetSSLPemCertEnvsC(x509), 0);
}

TEST_F(SSLTest, LitebusSetSSLPemCAEnvsCTest)
{
    EXPECT_EQ(LitebusSetSSLPemCAEnvsC(nullptr), -1);
    STACK_OF(X509) *caCerts = sk_X509_new_null();
    EXPECT_EQ(LitebusSetSSLPemCAEnvsC(caCerts), 0);
}

TEST_F(SSLTest, SetVerifyContextFromPemTest)
{
    SSL_ENVS *sslEnvs = new (std::nothrow) SSL_ENVS();
    SSL_CTX* sslCtx = SSL_CTX_new(TLS_client_method());

    // gen ca cert
    EVP_PKEY *caPkey = EVP_PKEY_new();
    X509 *caCert = X509_new();
    GenPemCert(caPkey, caCert);
    // gen pem cert
    EVP_PKEY *pkey = EVP_PKEY_new();
    X509 *x509 = X509_new();
    GenPemCert(pkey, x509, caPkey, caCert);
    // gen cert chain
    STACK_OF(X509) *caCerts = sk_X509_new_null();
    sk_X509_push(caCerts, caCert);

    sslEnvs->ca = caCerts;
    sslEnvs->cert = x509;
    sslEnvs->pkey = pkey;

    EXPECT_EQ(litebus::openssl::SetVerifyContextFromPem(sslEnvs, sslCtx), 1);
    delete sslEnvs;
}

TEST_F(SSLTest, VerifyIlegalPemTest)
{
    SSL_ENVS* sslEnvs = new(std::nothrow) SSL_ENVS();
    SSL_CTX* sslCtx = SSL_CTX_new(TLS_client_method());

    // gen ca cert
    EVP_PKEY* caPkey = EVP_PKEY_new();
    X509* caCert = X509_new();
    GenPemCert(caPkey, caCert);
    // gen pem cert
    EVP_PKEY* pkey = EVP_PKEY_new();
    X509* x509 = X509_new();
    GenPemCert(pkey, x509, caPkey, caCert, 12 * 3600L);
    // gen cert chain
    STACK_OF(X509)* caCerts = sk_X509_new_null();
    sk_X509_push(caCerts, caCert);

    sslEnvs->ca = caCerts;
    sslEnvs->cert = x509;
    sslEnvs->pkey = pkey;

    EXPECT_FALSE(litebus::openssl::SetVerifyContextFromPem(sslEnvs, sslCtx));
    delete sslEnvs;
}

}    // namespace litebus
