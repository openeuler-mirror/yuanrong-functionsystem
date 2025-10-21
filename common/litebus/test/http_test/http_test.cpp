#include "http_test/http_test.hpp"

std::string apiServerUrl = [] {
    uint16_t port = GetPortEnv("API_SERVER_PORT", 2227);
    return GetEnv("API_SERVER_PORT", "127.0.0.1") + ":" + std::to_string(port);
}();

std::string localUrl = [] {
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    return GetEnv("LITEBUS_PORT", "127.0.0.1") + ":" + std::to_string(port);
}();

std::string httpCurlUrl = [] {
    uint16_t port = GetPortEnv("API_SERVER_PORT", 2227);
    return "http://" + GetEnv("API_SERVER_PORT", "127.0.0.1") + ":" + std::to_string(port);
}();

int recvKmsgNum = 0;
int recvKhttpNum = 0;

// Implement of APIServer
void APIServer::CheckRequestClient(const Request &request)
{
    ASSERT_TRUE(request.client.IsSome());
    BUSLOG_INFO("request comes from {}", request.client.Get());
}

Future<Response> APIServer::HandleHttpRequest(const Request &request)
{
    BUSLOG_INFO("Hi, i have got your message which visit /API_Server/api/v1, body= {}", request.body);
    recvKhttpNum++;

    CheckRequestClient(request);

    HeaderMap headers = request.headers;
    // test application/json if user send a JSON content
    auto iter = headers.find("Content-Type");
    if (iter != headers.end() && headers["Content-Type"] == "application/json") {
        string response =
            "{"
            "  \"ip\": \"" +g_localip+"\","
            "  \"port\": 2227"
            "}";

        return Ok(response, ResponseBodyType::JSON);
    }

    return Response(ResponseCode::OK, request.body);
}

Future<Response> APIServer::HandleHttpRequest1(const Request &request)
{
    BUSLOG_INFO("Hi, i have got your message which visit /API_Server/api/v1, body= {}", request.body);
    recvKhttpNum++;
    CheckRequestClient(request);

    return Response(ResponseCode::CONFLICT, "Hi, i have got your mesaage which visit /API_Server/api/v2...");
}

Future<Response> APIServer::HandleHttpRequest2(const Request &request)
{
    BUSLOG_INFO("Hi, i have got your message which visit /API_Server/api/v1, body= {}", request.body);
    recvKhttpNum++;
    CheckRequestClient(request);

    return Response(ResponseCode::GONE, "Hi, i have got your mesaage which visit /API_Server/api/v3...");
}

Future<Response> APIServer::HandleHttpRequest3(const Request &request)
{
    BUSLOG_INFO("Hi, i have got your message which visit /API_Server/api/v1, body= {}", request.body);
    // test timeout
    CheckRequestClient(request);

    Future<Response> response;
    response.WaitFor(15000);
    recvKhttpNum++;

    return Response(ResponseCode::CONFLICT, "Hi, i have got your mesaage which visit /API_Server/api/v4...");
}

Future<Response> APIServer::HandleDefaultHttpRequest(const Request &request)
{
    BUSLOG_INFO("Hi, i have got your message which visit /..., client= {}", request.client.Get());
    recvKhttpNum++;
    CheckRequestClient(request);
    return Response(ResponseCode::REQUEST_TIMEOUT, "Hi, i have got your mesaage which visit /...");
}

void APIServer::handleHttpMsg(litebus::AID from, std::string &&type, std::string &&data)
{
    BUSLOG_INFO("receive ping data from {}, type: {}, data: {}", std::string(from), type, data);
    recvKmsgNum++;
    return;
}

// Implement of HTTPTest
void HTTPTest::SetUp()
{
    BUSLOG_INFO("Start http test.");
    apiServer = std::make_shared<APIServer>(apiServerName);
    litebus::Spawn(apiServer);
    if (ActorMgr::GetActorMgrRef()->GetActor(SYSMGR_ACTOR_NAME) == nullptr) {
        litebus::Spawn(std::make_shared<litebus::http::HttpSysMgr>(SYSMGR_ACTOR_NAME));
    }
}

void HTTPTest::TearDown()
{
    BUSLOG_INFO("Finish http test.");
    litebus::TerminateAll();

    recvKmsgNum = 0;
    recvKhttpNum = 0;
#ifdef LIBPROCESS_INTERWORK_ENABLED
    BUSLOG_INFO("Kill libprocess server....");
    if (libprocessServer.IsOK() && libprocessServer.Get() != nullptr) {
        ::kill(libprocessServer.Get()->GetPid(), 9);
    }
    litebus::Option<int> ret = litebus::os::Rmdir(libprocess_log_dir);
    if (ret.IsSome()) {
        BUSLOG_INFO("rm libprocess log dir ] ret= {}", ret.Get());
    }
#endif

#ifdef SSL_ENABLED
    BUSLOG_INFO("clean ssl envs...");
    litebus::openssl::SslFinalize();
#endif
}

bool HTTPTest::CheckRecvKmgNum(int expectedNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;    // us
    int usleepCount = 100000;                // 100ms

    while (timeout) {
        usleep(usleepCount);
        if (recvKmsgNum >= expectedNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }
    return false;
}

bool HTTPTest::CheckRecvReqNum(int expectedNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;    // us
    int usleepCount = 100000;                // 100ms

    while (timeout) {
        usleep(usleepCount);
        if (recvKhttpNum >= expectedNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }
    return false;
}

bool HTTPTest::CheckLinkNum(int expectedLinkNum, int _timeout)
{
    int timeout = _timeout * 1000 * 1000;    // us
    int usleepCount = 100000;                // 100ms
    LinkMgr *linkMgr = LinkMgr::GetLinkMgr();
    int linkNum = linkMgr->GetRemoteLinkCount();

    while (timeout) {
        usleep(usleepCount);
        linkNum = linkMgr->GetRemoteLinkCount();
        if (linkNum == expectedLinkNum) {
            return true;
        }
        timeout = timeout - usleepCount;
    }
    return false;
}

TEST_F(HTTPTest, Send1Kmg)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    // begin to send
    string data(10, 'A');
    AID from("testserver", localUrl);
    AID to(apiServerName, apiServerUrl);
    string msgName = "PingMessage";

    std::unique_ptr<MessageBase> msg(
        new MessageBase(from, to, std::move(msgName), std::move(data), MessageBase::Type::KMSG));

    io->Send(std::move(msg));

    ret = CheckRecvKmgNum(1, 5);
    ASSERT_TRUE(ret);

    io->UnLink(to);
    recvKmsgNum = 0;
}

TEST_F(HTTPTest, Send10Kmg)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    // begin to send
    string data(10, 'A');
    AID from("testserver", localUrl);
    AID to(apiServerName, apiServerUrl);
    string msgName = "PingMessage";
    std::unique_ptr<MessageBase> msg;

    int sendnum = 10;
    while (sendnum--) {
        data = string(10, 'A');
        msg =
            std::unique_ptr<MessageBase>(new MessageBase(from, to, msgName, std::move(data), MessageBase::Type::KMSG));
        io->Send(std::move(msg));
    }

    ret = CheckRecvKmgNum(10, 5);
    ASSERT_TRUE(ret);

    io->UnLink(to);
    recvKmsgNum = 0;
}

TEST_F(HTTPTest, CurlTestUsingDelegate)
{
    litebus::SetDelegate("APIServer");
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
        long http_code = 0;

        url = httpCurlUrl + "/api/v1";
        BUSLOG_INFO("url: {}", url);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1?country=china";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1?country=china";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1?country=china,company=futurewei";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1?country=china,company=futurewei";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1?country=china&company=futurewei";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1?country=china&company=futurewei";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1?country=china;company=futurewei";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1?country=china;company=futurewei";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1/fake_url";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1/fake_url";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 408);

        url = httpCurlUrl + "/APIServer/api/v1/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 408);

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
}

TEST_F(HTTPTest, CurlTestWithoutUsingDelegate)
{
    litebus::SetDelegate("");
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
        long http_code = 0;

        url = httpCurlUrl + "/APIServer/api/v1";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        BUSLOG_INFO("url: {}, http_code: {}", url, http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer@/api/v1";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 404);

        url = httpCurlUrl + "/APIServer/api/v1?country=china";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        // NOTE : because use add route for '/'
        url = httpCurlUrl + "/APIServer/api/v1/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 408);

        url = httpCurlUrl + "/APIServer/api/v1/aaa";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1/aaa//////";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1//aaa/bbb/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1//aaa/bbb//////";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1///";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 408);

        url = httpCurlUrl + "/APIServer/api/v1//aaa//bbb//ccc";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/APIServer/api/v1//aaa/bbb//ccc//////";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 200);

        url = httpCurlUrl + "/api/v1/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 404);

        url = httpCurlUrl + "/api/v1/aaa";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 404);

        url = httpCurlUrl + "/api/v1/aaa/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 404);

        url = httpCurlUrl + "/api/v2";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 404);

        url = httpCurlUrl + "/APIServer/api/v2";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 409);

        url = httpCurlUrl + "/APIServer/api/v3";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 410);

        url = httpCurlUrl + "/APIServer/api11111/v3";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 408);

        url = httpCurlUrl + "/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 404);

        url = httpCurlUrl + "/APIServer////aaa///";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        res = curl_easy_perform(curl);
        ASSERT_TRUE(res == CURLE_OK);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        ASSERT_TRUE(http_code == 408);

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
}

TEST_F(HTTPTest, PostRootPath)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer");
    litebus::Future<Response> response;
    std::string reqData = string(10, 'a');
    std::string contentType = "text/html";
    response = litebus::http::Post(url, None(), reqData, contentType);

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 408);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);
    recvKhttpNum = 0;

    url = URL("http", to.GetIp(), to.GetPort(), "/APIServer////");
    response = litebus::http::Post(url, None(), reqData, contentType);
    code = response.Get().retCode;
    ASSERT_TRUE(code == 408);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);
    recvKhttpNum = 0;
}

TEST_F(HTTPTest, Post)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");
    litebus::Future<Response> response;
    std::string reqData = string(1024 * 1024 * 10, 'a');
    std::string contentType = "text/html";
    response = litebus::http::Post(url, None(), reqData, contentType);

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, PostTimeOut)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    // set http request timeout : 10000 ms
    SetHttpRequestTimeOut(10000);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v4");
    litebus::Future<Response> response;
    std::string reqData = string(10, 'a');
    std::string contentType = "text/html";
    response = litebus::http::Post(url, None(), reqData, contentType);

    // we expect the connection should be closed in 10000ms
    response.WaitFor(15000);

    ASSERT_TRUE(response.IsError());
    BUSLOG_INFO("error code is: {}", response.GetErrorCode());
    ASSERT_TRUE(response.GetErrorCode() == 110);

    response = litebus::http::Post(url, None(), reqData, contentType, 3000);
    // we expect the connection should be closed in 3000ms
    response.WaitFor(5000);
    ASSERT_TRUE(response.IsError());
    BUSLOG_INFO("error code is: {}", response.GetErrorCode());
    ASSERT_TRUE(response.GetErrorCode() == 110);

    // set http request timeout : 90000 ms
    SetHttpRequestTimeOut(90000);
    recvKhttpNum = 0;
}

TEST_F(HTTPTest, GetTimeOut)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    // set http request timeout : 10000 ms
    SetHttpRequestTimeOut(10000);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v4");
    litebus::Future<Response> response;
    std::string contentType = "text/html";
    response = litebus::http::Get(url, None());

    // we expect the connection should be closed in 10000 ms
    response.WaitFor(15000);

    ASSERT_TRUE(response.IsError());
    BUSLOG_INFO("error code is: {}", response.GetErrorCode());
    ASSERT_TRUE(response.GetErrorCode() == 110);

    response = litebus::http::Get(url, None(), 3000);
    // we expect the connection should be closed in 3000ms
    response.WaitFor(5000);
    ASSERT_TRUE(response.IsError());
    BUSLOG_INFO("error code is: {}", response.GetErrorCode());
    ASSERT_TRUE(response.GetErrorCode() == 110);

    // set http request timeout : 90000 ms
    SetHttpRequestTimeOut(90000);
    recvKhttpNum = 0;
}

TEST_F(HTTPTest, PostWithPrefix)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "//////APIServer/api/v1");
    litebus::Future<Response> response;
    std::string reqData = string(1024 * 1024 * 10, 'a');
    std::string contentType = "text/html";
    response = litebus::http::Post(url, None(), reqData, contentType);

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, PostEmpty)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    // return 404 with no body
    URL url("http", to.GetIp(), to.GetPort(), "/api/v1");
    litebus::Future<Response> response;
    std::string reqData;
    std::string contentType = "text/html";
    response = litebus::http::Post(url, None(), reqData, contentType);

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 404);
    ASSERT_TRUE(response.Get().body.empty());

    ret = CheckRecvReqNum(0, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, Get)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");
    litebus::Future<Response> response;
    response = litebus::http::Get(url, None());

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, InvalidKmsgRequest)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/");
    std::unordered_map<std::string, std::string> headers;
    litebus::Future<Response> response;
    int code;

    // Post '/'
    headers["Litebus-From"] = "test@127.0.0.1:8080";
    url.path = "/";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post '//'
    url.path = "//";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post ''
    url.path = "";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post '    '
    url.path = "     ";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post 'abc'
    url.path = "abc";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post '/   /abc'
    url.path = "/   /abc";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post '/   /   '
    url.path = "/   /  ";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);

    // Post '/  '
    url.path = "/  ";
    response = litebus::http::Post(url, headers, None(), None());
    response.WaitFor(1000);
    ASSERT_TRUE(response.IsError());
    code = response.GetErrorCode();
    ASSERT_TRUE(code == 104);
}

TEST_F(HTTPTest, VlogToggle)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);
    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=3&duration=1000"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "POST";
    BUSLOG_INFO("request url path: {}", request.url.path);
    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);
    BUSLOG_INFO("toggle vlog success. v= {}", 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    ASSERT_TRUE(0 == orgVlog);
}

TEST_F(HTTPTest, VlogToggleErrMethod)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=3&duration=1000"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "GET";
    BUSLOG_DEBUG("request url path: {}", request.url.path);
    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 400);
}

TEST_F(HTTPTest, VlogToggleDurationNull)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);
    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=3"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "POST";
    BUSLOG_DEBUG("request url path: {}", request.url.path);
    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);
    ASSERT_TRUE(0 == orgVlog);
    BUSLOG_INFO("toggle vlog but duration is null. v= {}", 0);
}

TEST_F(HTTPTest, VlogToggleLevelInvalid)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=-1&duration=1000"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "POST";
    BUSLOG_DEBUG("request url path: {}", request.url.path);
    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code != 200);
    ASSERT_TRUE(0 == orgVlog);
    BUSLOG_INFO("toggle vlog but level < 0. v= {}", 0);
}

TEST_F(HTTPTest, VlogToggleLevelInvalid2)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=a&duration=aaa"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "POST";
    BUSLOG_DEBUG("request url path: {}", request.url.path);
    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code != 200);
    ASSERT_TRUE(0 == orgVlog);
    BUSLOG_INFO("toggle vlog but level < 0. v= {}", 0);
}

TEST_F(HTTPTest, VlogToggleDurationInvalid)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=3&duration=-1"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "POST";
    BUSLOG_DEBUG("request url path: {}", request.url.path);
    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code != 200);
    ASSERT_TRUE(0 == orgVlog);
    BUSLOG_INFO("toggle vlog but duration < 0. v= {}", 0);
}

TEST_F(HTTPTest, VlogToggleDurationInvalid1)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);
    int32_t orgVlog = 0;
    BUSLOG_INFO("orgVlog = {}", orgVlog);
    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(SYSMGR_ACTOR_NAME);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/SysManager/toggle?level=3&duration=abcde"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.url = url;
    request.method = "POST";
    BUSLOG_DEBUG("request url path: {}", request.url.path);

    for (auto iter = request.url.query.begin(); iter != request.url.query.end(); iter++) {
        BUSLOG_INFO("first url query: {}, second url query: {}", iter->first, iter->second);
    }

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code != 200);
    ASSERT_TRUE(0 == orgVlog);
    BUSLOG_INFO("toggle vlog but duration < 0. v= {}", 0);
}

TEST_F(HTTPTest, LaunchRequest)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LaunchRequest0)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/APIServer/api/v1"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);

    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LaunchRequest1)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";
    request.keepAlive = false;

    litebus::Future<Response> response;

    // case
    url.ip = None();
    request.url = url;
    response = litebus::http::LaunchRequest(request);
    ASSERT_TRUE(response.IsError());
    EXPECT_EQ(litebus::http::INVALID_REQUEST, response.GetErrorCode());
    url.ip = to.GetIp();
    request.url = url;

    // case
    url.port = None();
    request.url = url;
    response = litebus::http::LaunchRequest(request);
    ASSERT_TRUE(response.IsError());
    EXPECT_EQ(litebus::http::INVALID_REQUEST, response.GetErrorCode());
    url.port = to.GetPort();
    request.url = url;

    // case
    request.method = "";
    response = litebus::http::LaunchRequest(request);
    ASSERT_TRUE(response.IsError());
    EXPECT_EQ(litebus::http::INVALID_REQUEST, response.GetErrorCode());
    request.method = "POST";

    // case
    request.keepAlive = true;
    response = litebus::http::LaunchRequest(request);
    ASSERT_TRUE(response.IsError());
    EXPECT_EQ(litebus::http::INVALID_REQUEST, response.GetErrorCode());
    request.keepAlive = false;
}

TEST_F(HTTPTest, LaunchRequest2)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/APIServer"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 408);

    url.path = "/APIServer";
    request.url = url;
    response = litebus::http::LaunchRequest(request);
    code = response.Get().retCode;
    ASSERT_TRUE(code == 408);

    url.path = "/APIServer/ ";
    request.url = url;
    response = litebus::http::LaunchRequest(request);
    code = response.Get().retCode;
    ASSERT_TRUE(code == 408);

    url.path = "/APIServer ";
    request.url = url;
    response = litebus::http::LaunchRequest(request);
    code = response.Get().retCode;
    ASSERT_TRUE(code == 408);

    ret = CheckRecvReqNum(4, 5);
    ASSERT_TRUE(ret);

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LaunchRequestWithRespCallback)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/APIServer/api/v1"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";

    litebus::Future<Response> response;
    response = litebus::http::LaunchRequest(request, [&request](const http::Response *response) {
        if (!response->body.empty()) {
            EXPECT_EQ(response->body, request.body);
        }
    });
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);
    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LaunchRequestPatch)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    Try<URL> url1 = URL::Decode(string("http://") + to.GetIp() + string(":") + std::to_string(to.GetPort())
                                + string("/APIServer/api/v1"));
    ASSERT_TRUE(url1.IsOK());
    URL url = url1.Get();

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "PATCH";

    litebus::Future<Response> response;

    response = litebus::http::LaunchRequest(request);
    int code = response.Get().retCode;
    ASSERT_TRUE(code == 200);

    ret = CheckRecvReqNum(1, 5);
    ASSERT_TRUE(ret);
    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LaunchRequestOfJsonBody1)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.headers["Content-Type"] = "application/json";
    std::string jsonString =
        "{"
        "  \"query\": \"leader\""
        "}";
    request.body = jsonString;
    request.url = url;
    request.method = "POST";
    request.keepAlive = false;

    litebus::Future<Response> tmpResponse;
    tmpResponse = litebus::http::LaunchRequest(request);
    Response response = tmpResponse.Get();
    ASSERT_TRUE(response.retCode == ResponseCode::OK);

    string body = response.body;
    ASSERT_TRUE(body.find("\"ip\": \"" + g_localip + "\"") != std::string::npos);
    ASSERT_TRUE(body.find("\"port\": 2227") != std::string::npos);
}

TEST_F(HTTPTest, LaunchRequestOfJsonBody2)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.headers["COntent-typE"] = "application/XXX";
    request.headers["conTent-TyPe"] = "application/json";
    std::string jsonString =
        "{"
        "  \"query\": \"leader\""
        "}";
    request.body = jsonString;
    request.url = url;
    request.method = "POST";
    request.keepAlive = false;

    litebus::Future<Response> tmpResponse;
    tmpResponse = litebus::http::LaunchRequest(request);
    Response response = tmpResponse.Get();
    ASSERT_TRUE(response.retCode == ResponseCode::OK);

    string body = response.body;
    ASSERT_TRUE(body.find("\"ip\": \"" + g_localip + "\"") != std::string::npos);
    ASSERT_TRUE(body.find("\"port\": 2227") != std::string::npos);
}

TEST_F(HTTPTest, LaunchReqOnCon)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

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

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LaunchReqOnLongBody)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";
    request.keepAlive = true;

    litebus::Future<HttpConnect> connection = litebus::http::Connect(url);
    HttpConnect con = connection.Get();

    int sendnum = 2;
    litebus::Future<Response> response[sendnum];
    for (int i = 0; i < sendnum; i++) {
        request.body = std::string(10000, 'a');;
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

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, LongTimeNoComm)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    litebus::AID to;
    to.SetUrl(apiServerUrl);
    to.SetName(apiServerName);
    URL url("http", to.GetIp(), to.GetPort(), "/APIServer/api/v1");

    Request request;
    request.body = "xyz";
    request.url = url;
    request.method = "POST";
    request.keepAlive = true;

    litebus::Future<HttpConnect> connection = litebus::http::Connect(url);
    HttpConnect con = connection.Get();

    recvKhttpNum = 0;
    int sendnum = 1;
    litebus::Future<Response> response[sendnum];
    for (int i = 0; i < sendnum; i++) {
        request.body = std::to_string(i);
        response[i] = con.LaunchRequest(request);
        BUSLOG_INFO("recved resp");
    }

    for (int j = 0; j < sendnum; j++) {
        int code = response[j].Get().retCode;
        ASSERT_TRUE(code == 200);
    }

    ret = CheckRecvReqNum(sendnum, 5);
    ASSERT_TRUE(ret);

    char *linkRecycleEnv = getenv("LITEBUS_LINK_RECYCLE_PERIOD");
    if (linkRecycleEnv != nullptr) {
        ret = CheckLinkNum(1, 1);
        ASSERT_TRUE(ret);
        ret = CheckLinkNum(0, 6);
        ASSERT_TRUE(ret);
    } else {
        Future<bool> disconnect = con.Disconnect();
        ASSERT_TRUE(disconnect.Get());
    }

    recvKhttpNum = 0;
}

TEST_F(HTTPTest, HeaderTest)
{
    HeaderMap headerMap;
    headerMap["Abc"] = "1";
    headerMap["aBc"] = "2";
    headerMap["aBC"] = "3";

    ASSERT_TRUE(headerMap.size() == 1);

    bool flag = false;
    auto iter1 = headerMap.find("abc");
    if (iter1 != headerMap.end()) {
        BUSLOG_INFO("find 'abc' in st.");
        flag = true;
        ASSERT_TRUE(headerMap["abc"] == "3");
    }

    ASSERT_TRUE(flag);
}

TEST_F(HTTPTest, QueryTest)
{
    std::string scheme = "http";
    std::string ip = g_localip;
    uint16_t port = 5050;
    std::string path = "/path";
    std::unordered_map<string, string> queryData;
    Try<URL> url1 = URL::Decode("http://" + g_localip + ":5050/path?query1=111&query2=222");
    ASSERT_TRUE(url1.IsOK());
    ASSERT_TRUE(url1.Get().ip.Get() == ip);
    ASSERT_TRUE(url1.Get().port.Get() == port);
    ASSERT_TRUE(url1.Get().path == path);
    ASSERT_TRUE(url1.Get().query.size() == 2);

    Try<URL> url2 = URL::Decode("http://" + g_localip + ":5050/path");
    ASSERT_TRUE(url2.IsOK());
    ASSERT_TRUE(url2.Get().ip.Get() == ip);
    ASSERT_TRUE(url2.Get().port.Get() == port);
    ASSERT_TRUE(url2.Get().path == path);
    ASSERT_TRUE(url2.Get().query.size() == 0);

    Try<URL> url3 = URL::Decode("http://" + g_localip + ":5050");
    ASSERT_TRUE(url3.IsError());

    Try<URL> url4 = URL::Decode("http://" + g_localip + ":5050/");
    ASSERT_TRUE(url4.IsOK());
    ASSERT_TRUE(url4.Get().ip.Get() == ip);
    ASSERT_TRUE(url4.Get().port.Get() == port);
    ASSERT_TRUE(url4.Get().path == "/");
    ASSERT_TRUE(url4.Get().query.size() == 0);

    Try<URL> url5 = URL::Decode("http://:5050/path");
    ASSERT_TRUE(url5.IsError());

    Try<URL> url6 = URL::Decode("http://" + g_localip + ":/path");
    ASSERT_TRUE(url6.IsError());

    Try<URL> url7 = URL::Decode("httpp://" + g_localip + ":5050/path");
    ASSERT_TRUE(url7.IsError());

    Try<URL> url8 = URL::Decode("/path", false);
    ASSERT_TRUE(url8.IsOK());
    ASSERT_TRUE(url8.Get().ip.Get() == litebus::GetLitebusAddress().ip);
    ASSERT_TRUE(url8.Get().port.Get() == litebus::GetLitebusAddress().port);
    ASSERT_TRUE(url8.Get().path == path);
    ASSERT_TRUE(url8.Get().query.size() == 0);

    Try<URL> url9 = URL::Decode("http://" + g_localip + ":a/path");
    ASSERT_TRUE(url9.IsError());
}

TEST_F(HTTPTest, QueryTest2)
{
    std::string scheme = "http";
    std::string ip = "::1";
    uint16_t port = 5050;
    std::string path = "/path";
    std::unordered_map<string, string> queryData;
    Try<URL> url1 = URL::Decode("http://" + ip + ":5050/path?query1=%25&query2=%25");
    ASSERT_TRUE(url1.IsOK());
    ASSERT_TRUE(url1.Get().ip.Get() == ip);
    ASSERT_TRUE(url1.Get().port.Get() == port);
    ASSERT_TRUE(url1.Get().path == path);
    ASSERT_TRUE(url1.Get().query.size() == 2);
    for (const auto &queryItem : url1.Get().query) {
        ASSERT_TRUE(queryItem.second == "%");
    }

    Try<URL> url1_1 = URL::Decode("http://" + ip + ":5050/path?query1=A+%25&query2=A+%25");
    ASSERT_TRUE(url1_1.Get().query.size() == 2);
    for (const auto &queryItem : url1_1.Get().query) {
        ASSERT_TRUE(queryItem.second == "A %");
    }

    Try<URL> url1_2 = URL::Decode("http://" + ip + ":5050/path?query1=%");
    ASSERT_TRUE(url1_2.Get().query.size() == 0);

    Try<URL> url1_3 = URL::Decode("http://" + ip + ":5050/path?query1=%XX");
    ASSERT_TRUE(url1_3.Get().query.size() == 0);

    Try<URL> url1_4 = URL::Decode("http://" + ip + ":5050/path?query1=%25A");
    ASSERT_TRUE(url1_4.Get().query.size() == 1);
    for (const auto &queryItem : url1_4.Get().query) {
        ASSERT_TRUE(queryItem.second == "%A");
    }

    ip = "2001:da8:3000::183";
    Try<URL> url2 = URL::Decode("http://" + ip + ":5050/path");
    ASSERT_TRUE(url2.IsOK());
    ASSERT_TRUE(url2.Get().ip.Get() == ip);
    ASSERT_TRUE(url2.Get().port.Get() == port);
    ASSERT_TRUE(url2.Get().path == path);
    ASSERT_TRUE(url2.Get().query.size() == 0);

    ip = "[2001:da8:3000::183]";
    url1 = URL::Decode("http://" + ip + ":5050/path?query1=111&query2=222");
    BUSLOG_DEBUG("************** url1.Get().ip.Get(): {}", url1.Get().ip.Get());
    ASSERT_TRUE(url1.IsOK());
    ASSERT_TRUE(url1.Get().ip.Get() == "2001:da8:3000::183");
    ASSERT_TRUE(url1.Get().port.Get() == port);
    ASSERT_TRUE(url1.Get().path == path);
    ASSERT_TRUE(url1.Get().query.size() == 2);

    ip = "[::1]";
    url2 = URL::Decode("http://" + ip + ":5050/path");
    ASSERT_TRUE(url2.IsOK());
    ASSERT_TRUE(url2.Get().ip.Get() == "::1");
    ASSERT_TRUE(url2.Get().port.Get() == port);
    ASSERT_TRUE(url2.Get().path == path);
    ASSERT_TRUE(url2.Get().query.size() == 0);

    EXPECT_TRUE(URL::Decode("http://localhost/").IsOK());
    EXPECT_TRUE(URL::Decode("http://localhost/path").IsOK());

    EXPECT_TRUE(URL::Decode("http://localhost:80/").IsOK());
    EXPECT_TRUE(URL::Decode("http://localhost:80/path").IsOK());
}

TEST_F(HTTPTest, QueryTest3)
{
    std::string scheme = "http://";
    std::string ip = "::1";
    uint16_t port = 5050;
    std::string path = "/path";
    std::unordered_map<string, string> queryData;
    Try<URL> url1 = URL::Decode(scheme + ip + ":5050/path?query1=%25&query2=%25");
    ASSERT_TRUE(url1.IsOK());
    ASSERT_TRUE(url1.Get().ip.Get() == ip);
    ASSERT_TRUE(url1.Get().port.Get() == port);
    ASSERT_TRUE(url1.Get().path == path);
    ASSERT_TRUE(url1.Get().query.size() == 2);
    for (const auto &queryItem : url1.Get().query) {
        ASSERT_TRUE(queryItem.second == "%");
    }

    url1 = URL::Decode(scheme + ip + ":5050/path?query1=%25&query1=%25");
    ASSERT_TRUE(url1.Get().query.size() == 1);
    ASSERT_TRUE(url1.Get().rawQuery.size() == 1);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").size() == 2);
    for (const auto &queryItem : url1.Get().query) {
        ASSERT_TRUE(queryItem.second == "%");
    }
    for (const auto &query : url1.Get().rawQuery) {
        for(const auto &queryItem : query.second )
        ASSERT_TRUE(queryItem == "%");
    }
    url1 = URL::Decode(scheme + ip + ":5050/path?query1=%25&query1=%25&query2=%25");
    ASSERT_TRUE(url1.Get().query.size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query2").size() == 1);
    for (const auto &queryItem : url1.Get().query) {
        ASSERT_TRUE(queryItem.second == "%");
    }
    for (const auto &query : url1.Get().rawQuery) {
        for(const auto &queryItem : query.second )
        ASSERT_TRUE(queryItem == "%");
    }

    url1 = URL::Decode(scheme + ip + ":5050/path?query1=%25&query1&query2=111");
    ASSERT_TRUE(url1.Get().query.size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").at(0) == "%");
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").at(1) == "");
    ASSERT_TRUE(url1.Get().rawQuery.at("query2").size() == 1);
    ASSERT_TRUE(url1.Get().rawQuery.at("query2").at(0) == "111");

    ip = "[2001:da8:3000::183]";
    url1 = URL::Decode(scheme + ip + ":5050/path?query1=111&query1=222");
    ASSERT_TRUE(url1.IsOK());
    ASSERT_TRUE(url1.Get().ip.Get() == "2001:da8:3000::183");
    ASSERT_TRUE(url1.Get().port.Get() == port);
    ASSERT_TRUE(url1.Get().path == path);
    ASSERT_TRUE(url1.Get().query.size() == 1);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").at(0) == "111");
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").at(1) == "222");

    ip = "[2001:da8:3000::183]";
    url1 = URL::Decode(scheme + ip + ":5050/path?query1=111&query1=222&query2=333");
    ASSERT_TRUE(url1.IsOK());
    ASSERT_TRUE(url1.Get().ip.Get() == "2001:da8:3000::183");
    ASSERT_TRUE(url1.Get().port.Get() == port);
    ASSERT_TRUE(url1.Get().path == path);
    ASSERT_TRUE(url1.Get().query.size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").size() == 2);
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").at(0) == "111");
    ASSERT_TRUE(url1.Get().rawQuery.at("query1").at(1) == "222");
    ASSERT_TRUE(url1.Get().rawQuery.at("query2").size() == 1);
    ASSERT_TRUE(url1.Get().rawQuery.at("query2").at(0) == "333");
}

TEST_F(HTTPTest, CollectMetricsTest)
{
    std::unique_ptr<TCPMgr> io(new TCPMgr());
    io->Init();
    io->RegisterMsgHandle(&ActorMgr::Receive);
    bool ret = io->StartIOServer(apiServerUrl, apiServerUrl);
    ASSERT_TRUE(ret);

    io->CollectMetrics();
}
