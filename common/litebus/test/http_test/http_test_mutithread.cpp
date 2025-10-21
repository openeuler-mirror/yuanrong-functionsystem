#include "http_test/http_test.hpp"

#include "ssl/openssl_wrapper.hpp"

#include "ssl/ssl_env.hpp"

#include "timer/timewatch.hpp"

#include <thread>

namespace litebus {
namespace openssl {
bool SslInitInternal();
}    // namespace openssl
}    // namespace litebus

int sendnum = 1000;
const std::string TEST_HTTP_WAIT_TIMEOUT_STRING = "WAIT_TIME_OUT";

void PostThreadFun1(const URL &url, const Option<std::unordered_map<std::string, std::string>> &headers,
                    const Option<std::string> &body, const Option<std::string> &contentType,
                    const Option<int> &errCode, const std::string &errString)
{
    litebus::Future<Response> response;
    if (errString != TEST_HTTP_WAIT_TIMEOUT_STRING) {
        response = litebus::http::Post(url, headers, body, contentType);
        response.WaitFor(10000);
    } else {
        BUSLOG_INFO("begin wait ] begin time = {}", litebus::TimeWatch::Now());
        response.WaitFor(1000);
    }

    if (response.IsOK()) {
        int code = response.Get().retCode;
        BUSLOG_INFO("ok code is: ", code);
        EXPECT_EQ(code, errCode.Get());
    } else if (response.IsError()) {
        std::string message = litebus::http::GetHttpError(response.GetErrorCode());
        BUSLOG_INFO("error message is : {}, right message is {}", message, errString);
        EXPECT_EQ(message, errString);
    } else {
        BUSLOG_INFO("end wait ] end time = {}", litebus::TimeWatch::Now());
        EXPECT_EQ(TEST_HTTP_WAIT_TIMEOUT_STRING, errString);
    }
}

void SetEnvAndSetupWithCertType2()
{
    BUSLOG_INFO("start set env and set up");
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

    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_ENABLED", "1"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_KEY_FILE", keyPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_CERT_FILE", certPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_REQUIRE_CERT", "1"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_VERIFY_CERT", "1"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_CA_DIR", rootCertDirPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_CA_FILE", rootCertPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_DECRYPT_TYPE", "0"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_DECRYPT_DIR", decryptPath.c_str()));
    auto pKey = std::string("Msp-4102");
    litebus::SetPasswdForDecryptingPrivateKey(pKey.c_str(), pKey.length());
    ASSERT_TRUE(litebus::openssl::SslInitInternal());

    char privateKey[1000] = { '\0' };
    ASSERT_TRUE(litebus::GetPasswdForDecryptingPrivateKey(privateKey, 1000) == 0);
    char privateKeyError[512] = { '\0' };
    ASSERT_TRUE(litebus::GetPasswdForDecryptingPrivateKey(privateKeyError, 512) == -1);

    ASSERT_TRUE(std::string(privateKey) == std::string("Msp-4102"));

    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("https", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";
    request.keepAlive = true;

    litebus::Future<HttpConnect> connection = litebus::http::Connect(url);
    HttpConnect con = connection.Get();

    litebus::Future<Response> response[sendnum];
    for (int i = 0; i < sendnum; i++) {
        request.body = std::to_string(i);
        response[i] = con.LaunchRequest(request);
    }

    for (int j = 0; j < sendnum; j++) {
        int code = response[j].Get().retCode;
        ASSERT_TRUE(code == 200);
    }

    Future<bool> disconnect = con.Disconnect();
    ASSERT_TRUE(disconnect.Get());

    litebus::openssl::SslFinalize();
    BUSLOG_INFO("end set env and set up");
}

void SetMultiEnvAndSetupWithCertType()
{
    BUSLOG_INFO("start set env and set up");
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

    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_ENABLED", "1"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_KEY_FILE", keyPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_CERT_FILE", certPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_REQUIRE_CERT", "1"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_VERIFY_CERT", "1"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_CA_DIR", rootCertDirPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_CA_FILE", rootCertPath.c_str()));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_DECRYPT_TYPE", "0"));
    EXPECT_EQ(0, LitebusSetSSLEnvsC("LITEBUS_SSL_DECRYPT_DIR", decryptPath.c_str()));

    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_ENABLED", "1"));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_KEY_FILE", keyPath.c_str()));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_CERT_FILE", certPath.c_str()));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_REQUIRE_CERT", "1"));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_VERIFY_CERT", "1"));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_CA_DIR", rootCertDirPath.c_str()));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_CA_FILE", rootCertPath.c_str()));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_DECRYPT_TYPE", "0"));
    EXPECT_EQ(0, LitebusSetMultiSSLEnvsC("ssl2", "LITEBUS_SSL_DECRYPT_DIR", decryptPath.c_str()));

    auto pKey = std::string("Msp-4102");
    litebus::SetPasswdForDecryptingPrivateKey(pKey.c_str(), pKey.length());
    LitebusSetMultiPasswdForDecryptingPrivateKeyC("ssl2", pKey.c_str(), pKey.length());
    ASSERT_TRUE(litebus::openssl::SslInitInternal());

    char privateKey[1000] = { '\0' };
    ASSERT_TRUE(litebus::GetPasswdForDecryptingPrivateKey(privateKey, 1000) == 0);

    ASSERT_TRUE(std::string(privateKey) == std::string("Msp-4102"));

    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("https", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";
    request.keepAlive = true;
    request.credential = litebus::Option<std::string>("ssl2");

    litebus::Future<HttpConnect> connection = litebus::http::Connect(url, request.credential);
    HttpConnect con = connection.Get();

    litebus::Future<Response> response[sendnum];
    for (int i = 0; i < sendnum; i++) {
        request.body = std::to_string(i);
        response[i] = con.LaunchRequest(request);
    }

    for (int j = 0; j < sendnum; j++) {
        int code = response[j].Get().retCode;
        ASSERT_TRUE(code == 200);
    }

    Future<bool> disconnect = con.Disconnect();
    ASSERT_TRUE(disconnect.Get());

    litebus::openssl::SslFinalize();
    BUSLOG_INFO("start set env and set up");
}

TEST_F(HTTPTest, HttpsLaunchRequestOnEnvInType2)
{
    std::thread thread = std::thread(SetEnvAndSetupWithCertType2);
    thread.join();

    bool ret = CheckRecvReqNum(sendnum, 5);
    ASSERT_TRUE(ret);
    litebus::openssl::SslFinalize();
}

TEST_F(HTTPTest, HttpsLaunchRequestOnMultiEnv)
{
    std::thread thread = std::thread(SetMultiEnvAndSetupWithCertType);
    thread.join();

    bool ret = CheckRecvReqNum(sendnum, 5);
    ASSERT_TRUE(ret);
    litebus::openssl::SslFinalize();
}

TEST_F(HTTPTest, PostMutiThreads)
{
    litebus::AID to;
    to.SetUrl(localUrl);
    to.SetName(apiServerName);
    URL url1("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");
    URL url2("http", "127.0.0", 2237, "/APIServer/api/v1");
    URL url3("http", "127.0.0.1", 2237, "/APIServer/api/v1");
    std::string contentType = "text/html";
    std::string body = "";

    std::thread threads[150];
    int cnt = 0;
    for (int i = 0; i < 50; i++) {
        threads[cnt++] = std::thread(PostThreadFun1, url1, None(), body, contentType, 200, "");
        threads[cnt++] = std::thread(PostThreadFun1, url2, None(), body, contentType, None(), "Connection refused");
        threads[cnt++] = std::thread(PostThreadFun1, url3, None(), body, contentType, None(), "Connection refused");
    }

    EXPECT_EQ(150, cnt);

    for (auto &t : threads) {
        t.join();
    }

    bool ret = CheckRecvReqNum(50, 10);
    ASSERT_TRUE(ret);
}

TEST_F(HTTPTest, PostMutiThreadsWithBigData)
{
    litebus::AID to;
    to.SetUrl(localUrl);
    to.SetName(apiServerName);
    URL url1("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    std::string contentType = "text/html";
    std::string body = string(1024 * 1024, 'a');

    std::thread threads[10];
    int cnt = 0;
    for (int j = 0; j < 10; j++) {
        threads[cnt++] = std::thread(PostThreadFun1, url1, None(), body, contentType, 200, "");
    }

    EXPECT_EQ(10, cnt);

    for (auto &t : threads) {
        t.join();
    }

    bool ret = CheckRecvReqNum(10, 50);
    ASSERT_TRUE(ret);
}

TEST_F(HTTPTest, PostMutiThreadsWithTimeout)
{
    litebus::AID to;
    to.SetUrl(localUrl);
    to.SetName(apiServerName);
    URL url1("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    std::string contentType = "text/html";
    std::string body = "";

    std::thread threads[10];
    int cnt = 0;
    for (int i = 0; i < 5; i++) {
        threads[cnt++] = std::thread(PostThreadFun1, url1, None(), body, contentType, 200, "");
        threads[cnt++] =
            std::thread(PostThreadFun1, url1, None(), body, contentType, None(), TEST_HTTP_WAIT_TIMEOUT_STRING);
    }

    EXPECT_EQ(10, cnt);

    for (auto &t : threads) {
        t.join();
    }

    bool ret = CheckRecvReqNum(5, 25); /* 5(timeout)*5(thread count)*/
    ASSERT_TRUE(ret);
}
