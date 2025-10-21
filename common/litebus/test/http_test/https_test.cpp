#include "http_test/http_test.hpp"
#include "ssl/openssl_wrapper.hpp"
#include "exec/exec.hpp"
#include "exec/reap_process.hpp"

std::string httpsCurlUrl = [] {
    uint16_t port = GetPortEnv("API_SERVER_PORT", 2227);
    return GetEnv("API_SERVER_PORT", "127.0.0.1") + ":" + std::to_string(port);
}();

static const int32_t ERROR_CODE = -99;

namespace litebus {
namespace openssl {
bool SslInitInternal();
}
}    // namespace litebus

#ifdef LIBPROCESS_INTERWORK_ENABLED
const std::string libprocessPostRespTxtMsg = "responsed post";
const std::string libprocessGetRespTxtMsg = "responsed get";

std::map<std::string, std::string> ResetLDLibPath()
{
    std::map<std::string, std::string> environments = os::Environment();
    std::map<std::string, std::string> newEnv;
    size_t index = 0;
    // avoid  enviroment overflow size
    for (auto it = environments.begin(); index < environments.size(); ++it, index++) {
        std::string key = it->first;
        std::string value = it->second;
        if (key == "LD_LIBRARY_PATH") {
            value = std::string(::getenv("LIBPROCESS_GLOG_PATH"));
        }
        newEnv.insert(std::pair<std::string, std::string>(key, value));
    }
    // execvpe arg last param must be nullptr
    return newEnv;
}

Try<std::shared_ptr<Exec>> SetUpLibprocessServer(std::string ssl_enable, std::string out_with_https)
{
    std::string command = "GLOG_v=3 ./libprocess_server/libprocess_server_test ";
    command = command + std::string("--ssl_enabled=") + ssl_enable + std::string(" ");
    command = command + std::string("--out_with_https=") + out_with_https + std::string(" ");
    command = command + std::string("--log_dir=") + libprocess_log_dir + std::string(" ");
    command = command + std::string("--log_file=") + libprocess_log_file;
    std::map<std::string, std::string> newEnv = ResetLDLibPath();
    return Exec::CreateExec(command, newEnv, ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreateFDIO(STDOUT_FILENO),
                            ExecIO::CreateFileIO("/dev/null"), {}, {});
}
#endif

void SetLitebusHttpsTestEnv(DecryptType type, bool sslInitRet = true,
                            std::string rootStandardizd = "", std::string comStandardizd = "",
                            std::string dpkeyStandardizd = "", std::string dpdirStandardizd = "")
{
    const char *sslSandBox = getenv("LITEBUS_SSL_SANDBOX");
    ASSERT_TRUE(sslSandBox != nullptr);

    std::map<std::string, std::string> environment;

    switch (type) {
        case WITHOUT_DECRYPT:
        case UNKNOWN_DECRYPT: {
            std::string keyPath = std::string(sslSandBox) + "default_keys/server.key";
            std::string certPath = std::string(sslSandBox) + "default_keys/server.crt";
            BUSLOG_INFO("keyPath is {}, certPath is {}", keyPath, certPath);
            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_KEY_FILE"] = keyPath;
            environment["LITEBUS_SSL_CERT_FILE"] = certPath;
            break;
        }

        case OSS_DECRYPT: {
            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_REQUIRE_CERT"] = "1";
            environment["LITEBUS_SSL_VERIFY_CERT"] = "1";
            environment["LITEBUS_SSL_DECRYPT_TYPE"] = "1";
            break;
        }
        case HARES_DECRYPT: {
            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_REQUIRE_CERT"] = "1";
            environment["LITEBUS_SSL_VERIFY_CERT"] = "1";
            environment["LITEBUS_SSL_DECRYPT_TYPE"] = "2";
            break;
        }
        case OSS_DECRYPT_3LAYERS: {
            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_REQUIRE_CERT"] = "1";
            environment["LITEBUS_SSL_VERIFY_CERT"] = "1";
            environment["LITEBUS_SSL_DECRYPT_TYPE"] = "1";
            break;
        }
    }

    FetchSSLConfigFromMap(environment);
    ASSERT_EQ(litebus::openssl::SslInitInternal(), sslInitRet);
}

TEST_F(HTTPTest, HttpsPost)
{
    SetLitebusHttpsTestEnv(WITHOUT_DECRYPT);

    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("https", to.GetIp(), to.GetPort(), "/APIServer/api/v1");
    litebus::Future<Response> response;
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    response = litebus::http::Post(url, None(), reqData, contentType);
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);
    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);

    litebus::openssl::SslFinalize();
}

TEST_F(HTTPTest, HttpsLaunchRequestVerifyCertErroPath)
{
    SetLitebusHttpsTestEnv(HARES_DECRYPT, false, "", "", "", "moca_keys/../moca_keys/../");
    litebus::openssl::SslFinalize();

    SetLitebusHttpsTestEnv(HARES_DECRYPT, false, "", "", "", "moca_keys/../moca_keys/");
    litebus::openssl::SslFinalize();

    SetLitebusHttpsTestEnv(OSS_DECRYPT, false, "oss_keys/../oss_keys/", "", "", "");
    litebus::openssl::SslFinalize();

    SetLitebusHttpsTestEnv(OSS_DECRYPT, false, "", "oss_keys/../oss_keys/", "", "");
    litebus::openssl::SslFinalize();

    SetLitebusHttpsTestEnv(OSS_DECRYPT, false, "", "", "oss_keys/../oss_keys/", "");
    litebus::openssl::SslFinalize();

    SetLitebusHttpsTestEnv(OSS_DECRYPT, false, "oss_keys/len_err/", "oss_keys/len_err/", "", "");
    litebus::openssl::SslFinalize();
}

TEST_F(HTTPTest, HttpsCurlWithVerifyCertType2)
{
    SetLitebusHttpsTestEnv(WITHOUT_DECRYPT);

    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        string url = "";

        url = httpsCurlUrl + "/APIServer/api/v1";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // if client use 'curl -k htts://$IP:$PORT/APIServer/api/v1', return false
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        res = curl_easy_perform(curl);
        BUSLOG_INFO("ret code is {}", res);
        ASSERT_FALSE(res == CURLE_OK);
        curl_easy_cleanup(curl);
    }
    litebus::openssl::SslFinalize();
}

TEST_F(HTTPTest, HttpsLaunchRequestVerifyCertType2)
{
    SetLitebusHttpsTestEnv(WITHOUT_DECRYPT);

    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("https", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    // std::this_thread::sleep_for(std::chrono::milliseconds(1000000));

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";
    request.keepAlive = true;

    litebus::Future<HttpConnect> connection = litebus::http::Connect(url);
    HttpConnect con = connection.Get();

    int sendnum = 1000;
    litebus::Future<Response> response[sendnum];
    for (int i = 0; i < sendnum; i++) {
        request.body = std::to_string(i);
        response[i] = con.LaunchRequest(request);
    }

    for (int j = 0; j < sendnum; j++) {
        int code = response[j].Get().retCode;
        ASSERT_TRUE(code == 200);
    }

    ret = CheckRecvReqNum(sendnum, 5);
    ASSERT_TRUE(ret);

    Future<bool> disconnect = con.Disconnect();
    ASSERT_TRUE(disconnect.Get());
    litebus::openssl::SslFinalize();
}

TEST_F(HTTPTest, WorkMaterialV2)
{
    SetLitebusHttpsTestEnv(OSS_DECRYPT_3LAYERS, false, "", "oss_keys/material_v2_key/", "", "");

    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
}

#ifdef LIBPROCESS_INTERWORK_ENABLED

TEST_F(HTTPTest, HttpsLitebusPostWithLibprocessBigSize)
{
    libprocessServer = SetUpLibprocessServer("1", "1");
    EXPECT_GT(libprocessServer.Get()->GetPid(), 0);
    // This cannot be removed, we should wait libprocess server started
    sleep(2);

    SetLitebusHttpsTestEnv(HARES_DECRYPT);

    URL url1("https", "127.0.0.1", 44555, "/BigSize");

    // Test litebus send post to libprocess
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    litebus::Future<Response> response;
    int postNum = 5;
    for (int i = 0; i < postNum; i++) {
        response = litebus::http::Post(url1, None(), reqData, contentType);
        BUSLOG_INFO("libprocess returns]code = {}, body = {}", response.Get().retCode, response.Get().body.size());
        ASSERT_TRUE(response.Get().retCode == 200);
        ASSERT_TRUE(response.Get().body.size() == 1024 * 512);
    }

    BUSLOG_INFO("begin to kill libprocess server...");
    int result = ::kill(libprocessServer.Get()->GetPid(), 9);
    BUSLOG_INFO("begin to kill libprocess server, result = {}", result);
    libprocessServer.Get()->GetStatus().OnComplete([=]() { litebus::openssl::SslFinalize(); });
}

TEST_F(HTTPTest, HttpsLitebusPostWithLibprocess)
{
    libprocessServer = SetUpLibprocessServer("1", "1");
    EXPECT_GT(libprocessServer.Get()->GetPid(), 0);
    // This cannot be removed, we should wait libprocess server started
    sleep(2);

    SetLitebusHttpsTestEnv(HARES_DECRYPT);

    URL url1("https", "127.0.0.1", 44555, "/post");

    // Test litebus send post to libprocess
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    litebus::Future<Response> response;
    int postNum = 10;
    for (int i = 0; i < postNum; i++) {
        response = litebus::http::Post(url1, None(), reqData, contentType);
        BUSLOG_INFO("libprocess returns]code = {}, body = {}", response.Get().retCode, response.Get().body.size());
        ASSERT_TRUE(response.Get().retCode == 200);
        ASSERT_TRUE(response.Get().body == libprocessPostRespTxtMsg);
    }

    // Test libprocess send post to litebus
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    URL url2("https", "127.0.0.1", 44555, "/postback");
    response = litebus::http::Post(url2, None(), reqData, contentType);
    BUSLOG_INFO("libprocess returns]code = {}", response.Get().retCode);
    ASSERT_TRUE(response.Get().retCode == 200);

    ret = CheckRecvReqNum(1, 5);
    BUSLOG_INFO("recv response form libprocess]num = ", recvKhttpNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("begin to kill libprocess server...");
    int result = ::kill(libprocessServer.Get()->GetPid(), 9);
    BUSLOG_INFO("begin to kill libprocess server, result = {}", result);
    libprocessServer.Get()->GetStatus().OnComplete([=]() { litebus::openssl::SslFinalize(); });
}

TEST_F(HTTPTest, HttpsLitebusPostWithLibprocessDnGde)
{
    libprocessServer = SetUpLibprocessServer("1", "0");
    EXPECT_GT(libprocessServer.Get()->GetPid(), 0);

    // This cannot be removed, we should wait libprocess server started
    sleep(2);

    // Test litebus send post to libprocess
    URL url1("https", "127.0.0.1", 44555, "/post");
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    litebus::Future<Response> response;
    int postNum = 10;
    for (int i = 0; i < postNum; i++) {
        response = litebus::http::Post(url1, None(), reqData, contentType);
        BUSLOG_INFO("libprocess returns]code = {}, body = {}", response.Get().retCode, response.Get().body);
        ASSERT_TRUE(response.Get().retCode == 200);
        ASSERT_TRUE(response.Get().body == libprocessPostRespTxtMsg);
    }

    // Test libprocess send post to litebus
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    URL url2("https", "127.0.0.1", 44555, "/postback");
    response = litebus::http::Post(url2, None(), reqData, contentType);
    BUSLOG_INFO("libprocess returns]code = {}", response.Get().retCode);
    ASSERT_TRUE(response.Get().retCode == 200);

    ret = CheckRecvReqNum(1, 5);
    BUSLOG_INFO("recv response form libprocess]num = {}", recvKhttpNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("begin to kill libprocess server...");
    int result = ::kill(libprocessServer.Get()->GetPid(), 9);
    BUSLOG_INFO("begin to kill libprocess server, result = {}", result);
    libprocessServer.Get()->GetStatus().OnComplete([=]() { litebus::openssl::SslFinalize(); });
}

TEST_F(HTTPTest, HttpsLitebusDnGdePostWithLibprocess)
{
    libprocessServer = SetUpLibprocessServer("0", "0");
    EXPECT_GT(libprocessServer.Get()->GetPid(), 0);

    // This cannot be removed, we should wait libprocess server started
    sleep(2);

    SetLitebusHttpsTestEnv(HARES_DECRYPT);

    // Test litebus send post to libprocess
    URL url1("http", "127.0.0.1", 44555, "/post");
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    litebus::Future<Response> response;
    int postNum = 10;
    for (int i = 0; i < postNum; i++) {
        response = litebus::http::Post(url1, None(), reqData, contentType);
        BUSLOG_INFO("libprocess returns]code = {}, body = {}", response.Get().retCode, response.Get().body);
        ASSERT_TRUE(response.Get().retCode == 200);
        ASSERT_TRUE(response.Get().body == libprocessPostRespTxtMsg);
    }

    // Test libprocess send post to litebus
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    URL url2("http", "127.0.0.1", 44555, "/postback");
    response = litebus::http::Post(url2, None(), reqData, contentType);
    BUSLOG_INFO("libprocess returns]code = {}", response.Get().retCode);
    ASSERT_TRUE(response.Get().retCode == 200);

    ret = CheckRecvReqNum(1, 5);
    BUSLOG_INFO("recv response form libprocess]num = {}", recvKhttpNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("begin to kill libprocess server...");
    int result = ::kill(libprocessServer.Get()->GetPid(), 9);
    BUSLOG_INFO("begin to kill libprocess server, result = {}", result);
    libprocessServer.Get()->GetStatus().OnComplete([=]() { litebus::openssl::SslFinalize(); });
}

TEST_F(HTTPTest, HttpsLitebusDnGdePostWithLibprocessDnGde)
{
    libprocessServer = SetUpLibprocessServer("1", "0");
    EXPECT_GT(libprocessServer.Get()->GetPid(), 0);

    // This cannot be removed, we should wait libprocess server started
    sleep(2);

    SetLitebusHttpsTestEnv(HARES_DECRYPT);

    // Test litebus send post to libprocess
    URL url1("https", "127.0.0.1", 44555, "/post");
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    litebus::Future<Response> response;
    int postNum = 10;
    for (int i = 0; i < postNum; i++) {
        response = litebus::http::Post(url1, None(), reqData, contentType);
        BUSLOG_INFO("libprocess returns]code = {}, body = {}", response.Get().retCode, response.Get().body);
        ASSERT_TRUE(response.Get().retCode == 200);
        ASSERT_TRUE(response.Get().body == libprocessPostRespTxtMsg);
    }

    // Test libprocess send post to litebus
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    URL url2("https", "127.0.0.1", 44555, "/postback");
    response = litebus::http::Post(url2, None(), reqData, contentType);
    BUSLOG_INFO("libprocess returns]code = {}", response.Get().retCode);
    ASSERT_TRUE(response.Get().retCode == 200);

    ret = CheckRecvReqNum(1, 5);
    BUSLOG_INFO("recv response form libprocess]num = {}", recvKhttpNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("begin to kill libprocess server...");
    int result = ::kill(libprocessServer.Get()->GetPid(), 9);
    BUSLOG_INFO("begin to kill libprocess server, result = {}", result);
    libprocessServer.Get()->GetStatus().OnComplete([=]() { litebus::openssl::SslFinalize(); });
}

TEST_F(HTTPTest, HttpsLitebusPostWithLibprocessNoSsl)
{
    libprocessServer = SetUpLibprocessServer("0", "0");
    EXPECT_GT(libprocessServer.Get()->GetPid(), 0);

    // This cannot be removed, we should wait libprocess server started
    sleep(2);

    // Test litebus send post to libprocess
    URL url1("http", "127.0.0.1", 44555, "/post");
    std::string reqData = "xyz";
    std::string contentType = "text/html";
    litebus::Future<Response> response;
    int postNum = 10;
    for (int i = 0; i < postNum; i++) {
        response = litebus::http::Post(url1, None(), reqData, contentType);
        BUSLOG_INFO("libprocess returns]code = {}, body = {}", response.Get().retCode, response.Get().body);
        ASSERT_TRUE(response.Get().retCode == 200);
        ASSERT_TRUE(response.Get().body == libprocessPostRespTxtMsg);
    }

    // Test libprocess send post to litebus
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    URL url2("http", "127.0.0.1", 44555, "/postback");
    response = litebus::http::Post(url2, None(), reqData, contentType);
    BUSLOG_INFO("libprocess returns]code = {}", response.Get().retCode);
    ASSERT_TRUE(response.Get().retCode == 200);

    ret = CheckRecvReqNum(1, 5);
    BUSLOG_INFO("recv response form libprocess]num = {}", recvKhttpNum);
    ASSERT_TRUE(ret);

    BUSLOG_INFO("begin to kill libprocess server...");
    int result = ::kill(libprocessServer.Get()->GetPid(), 9);
    BUSLOG_INFO("begin to kill libprocess server, result = {}", result);
    libprocessServer.Get()->GetStatus().OnComplete([=]() { litebus::openssl::SslFinalize(); });
}

TEST_F(HTTPTest, ConnectEstablishedCallbackTest)
{
    HttpConnect *conn = new HttpConnect();
    URL url("http", "127.0.0.1", 44555, "/post");
    auto res = conn->ConnectEstablishedCallback(ERROR_CODE, url);
    EXPECT_TRUE(res.IsError());
}

TEST_F(HTTPTest, ConnectAndLaunchReqCallbackTest)
{
    HttpConnect *conn = new HttpConnect();
    Request request;
    auto res = conn->ConnectAndLaunchReqCallback(ERROR_CODE, request);
    EXPECT_TRUE(res.IsError());
}

TEST_F(HTTPTest, ConnectTest)
{
    URL url1("", "127.0.0.1", 44555, "/post");
    url1.scheme = litebus::None();
    auto res = litebus::http::Connect(url1);
    EXPECT_TRUE(res.IsError());

    URL url2("httpp", "127.0.0.1", 44555, "/post");
    res = litebus::http::Connect(url2);
    EXPECT_TRUE(res.IsError());
}

TEST_F(HTTPTest, LaunchRequestTest)
{
    URL url1("", "127.0.0.1", 44555, "/post");
    url1.scheme = litebus::None();
    Request request;
    request.body = "xyz";
    request.url = url1;
    request.method = "POST";

    auto res = litebus::http::LaunchRequest(request);
    EXPECT_TRUE(res.IsError());

    URL url2("httpp", "127.0.0.1", 44555, "/post");
    request.url = url2;
    res = litebus::http::LaunchRequest(request);
    EXPECT_TRUE(res.IsError());

    URL url3("http", "127.0.0.1", 44555, "/post");
    request.url = url3;
    request.headers["Connection"] = "keep-alive";
    res = litebus::http::LaunchRequest(request);
    EXPECT_TRUE(res.IsError());
}

TEST_F(HTTPTest, PostTest)
{
    URL url("http", "127.0.0.1", 44555, "/post");
    url.ip = litebus::None();
    auto res = litebus::http::Post(url, litebus::None(), litebus::None(), litebus::None(), litebus::None());
    EXPECT_TRUE(res.IsError());

    std::unordered_map<std::string, std::string> headers;
    headers["Connection"] = "close";
    string contentType = "application/json";
    res = litebus::http::Post(url, headers, litebus::None(), contentType, litebus::None());
    EXPECT_TRUE(res.IsError());
}

TEST_F(HTTPTest, GetTest)
{
    URL url("http", "127.0.0.1", 44555, "/post");
    url.ip = litebus::None();
    std::unordered_map<std::string, std::string> headers;
    headers["Connection"] = "close";
    auto res = litebus::http::Get(url, headers, litebus::None());
    EXPECT_TRUE(res.IsError());
}

TEST_F(HTTPTest, GetHttpErrorTest)
{
    auto res = litebus::http::GetHttpError(0);
    EXPECT_EQ(res, "Unknown error.");
}

#endif
