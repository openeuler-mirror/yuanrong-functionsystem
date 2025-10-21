#include <gtest/gtest.h>
#define private public
#include "httpd/http.hpp"
#include "httpd/http_decoder.hpp"
#include "httpd/http_parser.hpp"

using litebus::http::HeaderMap;

using litebus::http::HttpMethod;
using litebus::http::HttpParser;
using litebus::http::HttpParserType;

using litebus::http::HTTP_DELETE;
using litebus::http::HTTP_GET;
using litebus::http::HTTP_POST;
using litebus::http::HTTP_PUT;
using litebus::http::HTTP_UNKNOWN;

using litebus::http::HTTP_REQUEST;
using litebus::http::HTTP_RESPONSE;
using namespace std;
using namespace litebus;
using namespace litebus::http;

class HttpParseMessage {
public:
    std::string name;    // for debugging purposes
    std::string raw;

    enum HttpParserType type;
    enum HttpMethod method;

    int statusCode;
    int numHeaders;

    std::string scheme;
    std::string requestPath;
    std::string requestUrl;
    std::string fragment;
    std::string queryString;

    std::string responseStatus;

    std::string body;
    std::string upgrade;    // upgraded body

    std::string host;
    std::string userInfo;
    uint16_t port = 0;

    enum { NONE = 0, FIELD, VALUE } lastHeaderElement;

    HeaderMap headers;
    std::unordered_map<std::string, std::string> query;

    bool shouldKeepAlive;
    bool messageCompleteOnEof;

    int numChunks;
    int numChunksComplete;
    std::vector<int> chunkLengths;

    unsigned short httpMajor;
    unsigned short httpMinor;

    int messageBeginCbCalled;
    int headersCompleteCbCalled;
    int messageCompleteCbCalled;

    int bodyIsFinal;
};

TEST(HttpParserTest, RequestTest00)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/test=3?test=3 HTTP/1.1\r\n"
        "User-Agent: curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1\r\n"
        "Host: 0.0.0.0=5000\r\n"
        "Accept: */*\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/test=3";
    request.headers = {
        { "User-Agent", "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" },
        { "Host", "0.0.0.0=5000" },
        { "Accept", "*/*" },
    };
    request.query = {
        { "test", "3" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest01)
{
    string requestRowString =
        "GET https://192.168.0.1:5000/favicon.ico HTTP/1.1\r\n"
        "Host: 0.0.0.0=5000\r\n"
        "User-Agent: Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-us,en;q=0.5\r\n"
        "Accept-Encoding: gzip,deflate\r\n"
        "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
        "Keep-Alive: 300\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "https";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/favicon.ico";
    request.headers = {
        { "Host", "0.0.0.0=5000" },
        { "User-Agent", "Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0" },
        { "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" },
        { "Accept-Language", "en-us,en;q=0.5" },
        { "Accept-Encoding", "gzip,deflate" },
        { "Accept-Charset", "ISO-8859-1,utf-8;q=0.7,*;q=0.7" },
        { "Keep-Alive", "300" },
        { "Connection", "keep-alive" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest02)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/dumbfuck HTTP/1.1\r\n"
        "aaaaaaaaaaaaa:++++++++++\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/dumbfuck";
    request.headers = {
        { "aaaaaaaaaaaaa", "++++++++++" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest03)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/forums/1/topics/2375?page=1 HTTP/1.1\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/forums/1/topics/2375";
    request.headers = {};
    request.query = {
        { "page", "1" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest04)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/get_no_headers_no_body/world HTTP/1.1\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/get_no_headers_no_body/world";
    request.headers = {};
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest05)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/get_one_header_no_body HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/get_one_header_no_body";
    request.headers = {
        { "Accept", "*/*" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest06)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/get_funky_content_length_body_hello HTTP/1.0\r\n"
        "conTENT-Length: 5\r\n"
        "\r\n"
        "HELLO";
    HttpParseMessage request;
    request.shouldKeepAlive = false;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/get_funky_content_length_body_hello";
    request.headers = {
        { "conTENT-Length", "5" },
    };
    request.query = {};
    request.body = "HELLO";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest07)
{
    string requestRowString1 =
        "POST http://192.168.0.1:5000/post_identity_body_world?q=search&page=123 HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: identity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "World";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/post_identity_body_world";
    request.headers = {
        { "Accept", "*/*" }, { "Transfer-Encoding", "identity" }, { "Content-Length", "5" },
    };
    request.query = {
        { "q", "search" }, { "page", "123" },
    };
    request.body = "World";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString1.data(), requestRowString1.length());

    ASSERT_TRUE(requests.size() == 1);

    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest08)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "1e\r\nall your base are belong to us\r\n"
        "0\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/post_chunked_all_your_base";
    request.headers = {
        { "Transfer-Encoding", "chunked" },
    };
    request.query = {};
    request.body = "all your base are belong to us";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest09)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/two_chunks_mult_zero_end HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "000\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/two_chunks_mult_zero_end";
    request.headers = {
        { "Transfer-Encoding", "chunked" },
    };
    request.query = {};
    request.body = "hello world";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest10)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/chunked_w_trailing_headers HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n"
        "Vary: *\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/chunked_w_trailing_headers";
    request.headers = {
        { "Transfer-Encoding", "chunked" }, { "Vary", "*" }, { "Content-Type", "text/plain" },
    };
    request.query = {};
    request.body = "hello world";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest11)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/chunked_w_bullshit_after_length HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5; ihatew3;whatthefuck=aretheseparametersfor\r\nhello\r\n"
        "6; blahblah; blah\r\n world\r\n"
        "0\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/chunked_w_bullshit_after_length";
    request.headers = {
        { "Transfer-Encoding", "chunked" },
    };
    request.query = {};
    request.body = "hello world";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest12)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/with_\"stupid\"_quotes?foo=\"bar\"&dump=\"var\" HTTP/1.1\r\n\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/with_\"stupid\"_quotes";
    request.headers = {};
    request.query = {
        { "foo", "\"bar\"" }, { "dump", "\"var\"" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest13)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/test HTTP/1.0\r\n"
        "Host: 0.0.0.0:5000\r\n"
        "User-Agent: ApacheBench/2.3\r\n"
        "Accept: */*\r\n\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = false;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/test";
    request.headers = {
        { "Host", "0.0.0.0:5000" }, { "User-Agent", "ApacheBench/2.3" }, { "Accept", "*/*" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest14)
{
    string requestRowString = "GET http://192.168.0.1:5000/test.cgi?foo=bar?baz HTTP/1.1\r\n\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/test.cgi";
    request.headers = {};
    request.query = {
        { "foo", "bar?baz" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest15)
{
    string requestRowString = "\r\nGET http://192.168.0.1:5000/test HTTP/1.1\r\n\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/test";
    request.headers = {};
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest16)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key2: 12998 5 Y3 1  .P00\r\n"
        "Sec-WebSocket-Protocol: sample\r\n"
        "Upgrade: WebSocket\r\n"
        "Sec-WebSocket-Key1: 4 @1  46546xW%0l 1 5\r\n"
        "Origin: http://192.168.0.1:5000/example.com\r\n"
        "\r\n"
        "Hot diggity dogg";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/demo";
    request.headers = {
        { "Host", "example.com" },
        { "Connection", "Upgrade" },
        { "Sec-WebSocket-Key2", "12998 5 Y3 1  .P00" },
        { "Sec-WebSocket-Protocol", "sample" },
        { "Upgrade", "WebSocket" },
        { "Sec-WebSocket-Key1", "4 @1  46546xW%0l 1 5" },
        { "Origin", "http://192.168.0.1:5000/example.com" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest17)
{
    string requestRowString1 =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Keep-Alive: 300\r\n"
        "\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4"
        "keep-alive\r\n"
        "\r\n";
    RequestDecoder *decoder1 = new RequestDecoder();
    deque<Request *> requests1 = decoder1->Decode(requestRowString1.data(), requestRowString1.length());
    ASSERT_TRUE(requests1.size() == 0);

    string requestRowString2 =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Keep-Alive: 300\r\n"
        "11111111111111111111111"
        "keep-alive\r\n"
        "\r\n";
    RequestDecoder *decoder2 = new RequestDecoder();
    deque<Request *> requests2 = decoder2->Decode(requestRowString2.data(), requestRowString2.length());
    ASSERT_TRUE(requests2.size() == 0);

    string requestRowString3 =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Keep-Alive: 300\r\n"
        "keep-alive\r\n"
        "\r\n";
    RequestDecoder *decoder3 = new RequestDecoder();
    deque<Request *> requests3 = decoder3->Decode(requestRowString3.data(), requestRowString3.length());
    ASSERT_TRUE(requests3.size() == 0);

    string requestRowString4 =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Keep-Alive: 300\r\n"
        "Ckeep-alive\r\n"
        "\r\n";
    RequestDecoder *decoder4 = new RequestDecoder();
    deque<Request *> requests4 = decoder4->Decode(requestRowString4.data(), requestRowString4.length());
    ASSERT_TRUE(requests4.size() == 0);

    string requestRowString5 =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Keep-Alive: 300\r\n"
        "Connection: Connection: Connecti"
        "keep-alive\r\n"
        "\r\n";
    RequestDecoder *decoder5 = new RequestDecoder();
    deque<Request *> requests5 = decoder5->Decode(requestRowString5.data(), requestRowString5.length());
    ASSERT_TRUE(requests5.size() == 1);

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/demo";
    request.headers = {
        { "Keep-Alive", "300" }, { "Connection", "Connection: Connectikeep-alive" },
    };
    request.query = {};
    request.body = "";
    EXPECT_EQ(requests5[0]->method, "GET");
    EXPECT_EQ(requests5[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests5[0]->headers, request.headers);
    EXPECT_EQ(requests5[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests5[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests5[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests5[0]->url.path, request.requestPath);
    EXPECT_EQ(requests5[0]->url.query, request.query);
    EXPECT_EQ(requests5[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest18)
{
    string requestRowString =
        "REPORT /test HTTP/1.1\r\n"
        "\r\n";
    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());
    ASSERT_TRUE(requests.size() == 0);
}

TEST(HttpParserTest, RequestTest19)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = false;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/";
    request.headers = {};
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest20)
{
    string requestRowString =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: \"ssdp:all\"\r\n"
        "\r\n";
    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());
    ASSERT_TRUE(requests.size() == 0);
}

TEST(HttpParserTest, RequestTest21)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/ HTTP/1.1\r\n"
        "Line1:   abc\r\n"
        "\tdef\r\n"
        " ghi\r\n"
        "\t\tjkl\r\n"
        "  mno \r\n"
        "\t \tqrs\r\n"
        "Line2: \t line2\t\r\n"
        "Line3:\r\n"
        " line3\r\n"
        "Line4: \r\n"
        " \r\n"
        "Connection:\r\n"
        " close\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = false;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/";
    request.headers = {
        { "Line1", "abc\tdef ghi\t\tjkl  mno \t \tqrs" },
        { "Line2", "line2\t" },
        { "Line3", "line3" },
        { "Line4", "" },
        { "Connection", "close" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest22)
{
    string requestRowString =
        "GET https://192.168.0.1:5000/hypnotoad.org?hail=all HTTP/1.1\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "https";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/hypnotoad.org";
    request.headers = {};
    request.query = {
        { "hail", "all" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest23)
{
    string requestRowString =
        "GET http://192.168.0.1:1234/hypnotoad.org:1234?hail=all HTTP/1.1\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 1234;
    request.requestPath = "/hypnotoad.org:1234";
    request.headers = {};
    request.query = {
        { "hail", "all" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest24)
{
    string requestRowString =
        "GET http://192.168.0.1:1234/hypnotoad.org:1234 HTTP/1.1\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 1234;
    request.requestPath = "/hypnotoad.org:1234";
    request.headers = {};
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest27)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/δ¶/δt/pope?q=1 HTTP/1.1\r\n"
        "Host: github.com\r\n"
        "\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/δ¶/δt/pope";
    request.headers = {
        { "Host", "github.com" },
    };
    request.query = {
        { "q", "1" },
    };
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest28)
{
    string requestRowString =
        "CONNECT home_0.netscape.com:443 HTTP/1.0\r\n"
        "User-agent: Mozilla/1.1N\r\n"
        "Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
        "\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());
    ASSERT_TRUE(requests.size() == 0);
}

TEST(HttpParserTest, RequestTest29)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/ HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "q=42\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/";
    request.headers = {
        { "Host", "www.example.com" },
        { "Content-Type", "application/x-www-form-urlencoded" },
        { "Content-Length", "4" },
    };
    request.query = {};
    request.body = "q=42";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest30)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/ HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 4\r\n"
        "Connection: close\r\n"
        "\r\n"
        "q=42\r\n";
    HttpParseMessage request;
    request.shouldKeepAlive = false;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/";
    request.headers = {
        { "Host", "www.example.com" },
        { "Content-Type", "application/x-www-form-urlencoded" },
        { "Content-Length", "4" },
        { "Connection", "close" },
    };
    request.query = {};
    request.body = "q=42";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest31)
{
    string requestRowString =
        "PURGE /file.txt HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());
    ASSERT_TRUE(requests.size() == 0);
}

TEST(HttpParserTest, RequestTest32)
{
    string requestRowString =
        "SEARCH / HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());
    ASSERT_TRUE(requests.size() == 0);
}

TEST(HttpParserTest, RequestTest33)
{
    string requestRowString =
        "GET http://192.168.0.1:1234/a%12ab!&*$@hypnotoad.org:1234/toto HTTP/1.1\r\n"
        "\r\n";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 1234;
    request.requestPath = "/a%12ab!&*$@hypnotoad.org:1234/toto";
    request.headers = {};
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest34)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/ HTTP/1.1\n"
        "Line1:   abc\n"
        "\tdef\n"
        " ghi\n"
        "\t\tjkl\n"
        "  mno \n"
        "\t \tqrs\n"
        "Line2: \t line2\t\n"
        "Line3:\n"
        " line3\n"
        "Line4: \n"
        " \n"
        "Connection:\n"
        " close\n"
        "\n";

    HttpParseMessage request;
    request.shouldKeepAlive = false;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/";
    request.headers = {
        { "Line1", "abc\tdef ghi\t\tjkl  mno \t \tqrs" },
        { "Line2", "line2\t" },
        { "Line3", "line3" },
        { "Line4", "" },
        { "Connection", "close" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest35)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: Something,\r\n"
        " Upgrade, ,Keep-Alive\r\n"
        "Sec-WebSocket-Key2: 12998 5 Y3 1  .P00\r\n"
        "Sec-WebSocket-Protocol: sample\r\n"
        "Upgrade: WebSocket\r\n"
        "Sec-WebSocket-Key1: 4 @1  46546xW%0l 1 5\r\n"
        "Origin: http://192.168.0.1:5000/example.com\r\n"
        "\r\n"
        "Hot diggity dogg";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/demo";
    request.headers = {
        { "Host", "example.com" },
        { "Connection", "Something, Upgrade, ,Keep-Alive" },
        { "Sec-WebSocket-Key2", "12998 5 Y3 1  .P00" },
        { "Sec-WebSocket-Protocol", "sample" },
        { "Upgrade", "WebSocket" },
        { "Sec-WebSocket-Key1", "4 @1  46546xW%0l 1 5" },
        { "Origin", "http://192.168.0.1:5000/example.com" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest36)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Connection: keep-alive, upgrade\r\n"
        "Upgrade: WebSocket\r\n"
        "\r\n"
        "Hot diggity dogg";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/demo";
    request.headers = {
        { "Connection", "keep-alive, upgrade" }, { "Upgrade", "WebSocket" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest37)
{
    string requestRowString =
        "GET http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Connection: keep-alive, \r\n upgrade\r\n"
        "Upgrade: WebSocket\r\n"
        "\r\n"
        "Hot diggity dogg";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/demo";
    request.headers = {
        { "Connection", "keep-alive,  upgrade" }, { "Upgrade", "WebSocket" },
    };
    request.query = {};
    request.body = "";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "GET");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, RequestTest38)
{
    string requestRowString =
        "POST http://192.168.0.1:5000/demo HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: HTTP/2.0\r\n"
        "Content-Length: 15\r\n"
        "\r\n"
        "sweet post body"
        "Hot diggity dogg";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/demo";
    request.headers = {
        { "Host", "example.com" }, { "Connection", "Upgrade" }, { "Upgrade", "HTTP/2.0" }, { "Content-Length", "15" },
    };
    request.query = {};
    request.body = "sweet post body";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.scheme.Get(), request.scheme);
    EXPECT_EQ(requests[0]->url.ip.Get(), request.host);
    EXPECT_EQ(requests[0]->url.port.Get(), request.port);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
}

TEST(HttpParserTest, ResponseTest00)
{
    string responseRowString =
        "HTTP/1.1 301 Moved Permanently\r\n"
        "Location: http://www.google.com/\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Date: Sun, 26 Apr 2009 11:11:49 GMT\r\n"
        "Expires: Tue, 26 May 2009 11:11:49 GMT\r\n"
        "X-$PrototypeBI-Version: 1.6.0.3\r\n" /* $ char in header field */
        "Cache-Control: public, max-age=2592000\r\n"
        "Server: gws\r\n"
        "Content-Length:  219  \r\n"
        "\r\n"
        "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
        "<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
        "<H1>301 Moved</H1>\n"
        "The document has moved\n"
        "<A HREF=\"http://www.google.com/\">here</A>.\r\n"
        "</BODY></HTML>\r\n";
    HttpParseMessage response;
    response.statusCode = 301;
    response.headers = {
        { "Location", "http://www.google.com/" },
        { "Content-Type", "text/html; charset=UTF-8" },
        { "Date", "Sun, 26 Apr 2009 11:11:49 GMT" },
        { "Expires", "Tue, 26 May 2009 11:11:49 GMT" },
        { "X-$PrototypeBI-Version", "1.6.0.3" },
        { "Cache-Control", "public, max-age=2592000" },
        { "Server", "gws" },
        { "Content-Length", "219  " },
    };
    response.body =
        "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
        "<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
        "<H1>301 Moved</H1>\n"
        "The document has moved\n"
        "<A HREF=\"http://www.google.com/\">here</A>.\r\n"
        "</BODY></HTML>\r\n";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest01)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Date: Tue, 04 Aug 2009 07:59:32 GMT\r\n"
        "Server: Apache\r\n"
        "X-Powered-By: Servlet/2.5 JSP/2.1\r\n"
        "Content-Type: text/xml; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        "  <SOAP-ENV:Body>\n"
        "    <SOAP-ENV:Fault>\n"
        "       <faultcode>SOAP-ENV:Client</faultcode>\n"
        "       <faultstring>Client Error</faultstring>\n"
        "    </SOAP-ENV:Fault>\n"
        "  </SOAP-ENV:Body>\n"
        "</SOAP-ENV:Envelope>";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Date", "Tue, 04 Aug 2009 07:59:32 GMT" },
        { "Server", "Apache" },
        { "X-Powered-By", "Servlet/2.5 JSP/2.1" },
        { "Content-Type", "text/xml; charset=utf-8" },
        { "Connection", "close" },
    };
    response.body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        "  <SOAP-ENV:Body>\n"
        "    <SOAP-ENV:Fault>\n"
        "       <faultcode>SOAP-ENV:Client</faultcode>\n"
        "       <faultstring>Client Error</faultstring>\n"
        "    </SOAP-ENV:Fault>\n"
        "  </SOAP-ENV:Body>\n"
        "</SOAP-ENV:Envelope>";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest02)
{
    string responseRowString = "HTTP/1.1 404 Not Found\r\n\r\n";
    HttpParseMessage response;
    response.statusCode = 404;
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    ASSERT_TRUE(responses[0]->headers.size() == 0);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest03)
{
    string responseRowString = "HTTP/1.1 301\r\n\r\n";
    HttpParseMessage response;
    response.statusCode = 301;
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    ASSERT_TRUE(responses[0]->headers.size() == 0);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest04)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "25  \r\n"
        "This is the data in the first chunk\r\n"
        "\r\n"
        "1C\r\n"
        "and this is the second one\r\n"
        "\r\n"
        "0  \r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Content-Type", "text/plain" }, { "Transfer-Encoding", "chunked" },
    };
    response.body =
        "This is the data in the first chunk\r\n"
        "and this is the second one\r\n";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest05)
{
    string responseRowString =
        "HTTP/1.1 200 OK\n"
        "Content-Type: text/html; charset=utf-8\n"
        "Connection: close\n"
        "\n"
        "these headers are from http://news.ycombinator.com/";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Content-Type", "text/html; charset=utf-8" }, { "Connection", "close" },
    };
    response.body = "these headers are from http://news.ycombinator.com/";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest06)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: 11\r\n"
        "Proxy-Connection: close\r\n"
        "Date: Thu, 31 Dec 2009 20:55:48 +0000\r\n"
        "\r\n"
        "hello world";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Content-Type", "text/html; charset=UTF-8" },
        { "Content-Length", "11" },
        { "Proxy-Connection", "close" },
        { "Date", "Thu, 31 Dec 2009 20:55:48 +0000" },
    };
    response.body = "hello world";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest07)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Server: DCLK-AdSvr\r\n"
        "Content-Type: text/xml\r\n"
        "Content-Length: 0\r\n"
        "DCLK_imp: v7;x;114750856;0-0;0;17820020;0/0;21603567/21621457/1;;~okv=;dcmt=text/xml;;~cs=o\r\n\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Server", "DCLK-AdSvr" },
        { "Content-Type", "text/xml" },
        { "Content-Length", "0" },
        { "DCLK_imp", "v7;x;114750856;0-0;0;17820020;0/0;21603567/21621457/1;;~okv=;dcmt=text/xml;;~cs=o" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest08)
{
    string responseRowString =
        "HTTP/1.0 301 Moved Permanently\r\n"
        "Date: Thu, 03 Jun 2010 09:56:32 GMT\r\n"
        "Server: Apache/2.2.3 (Red Hat)\r\n"
        "Cache-Control: public\r\n"
        "Pragma: \r\n"
        "Location: http://www.bonjourmadame.fr/\r\n"
        "Vary: Accept-Encoding\r\n"
        "Content-Length: 0\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 301;
    response.headers = {
        { "Date", "Thu, 03 Jun 2010 09:56:32 GMT" },
        { "Server", "Apache/2.2.3 (Red Hat)" },
        { "Cache-Control", "public" },
        { "Pragma", "" },
        { "Location", "http://www.bonjourmadame.fr/" },
        { "Vary", "Accept-Encoding" },
        { "Content-Length", "0" },
        { "Content-Type", "text/html; charset=UTF-8" },
        { "Connection", "keep-alive" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest09)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Date: Tue, 28 Sep 2010 01:14:13 GMT\r\n"
        "Server: Apache\r\n"
        "Cache-Control: no-cache, must-revalidate\r\n"
        "Expires: Mon, 26 Jul 1997 05:00:00 GMT\r\n"
        ".et-Cookie: PlaxoCS=1274804622353690521; path=/; domain=.plaxo.com\r\n"
        "Vary: Accept-Encoding\r\n"
        "_eep-Alive: timeout=45\r\n" /* semantic value ignored */
        "_onnection: Keep-Alive\r\n" /* semantic value ignored */
        "Transfer-Encoding: chunked\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "0\r\n\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Date", "Tue, 28 Sep 2010 01:14:13 GMT" },
        { "Server", "Apache" },
        { "Cache-Control", "no-cache, must-revalidate" },
        { "Expires", "Mon, 26 Jul 1997 05:00:00 GMT" },
        { ".et-Cookie", "PlaxoCS=1274804622353690521; path=/; domain=.plaxo.com" },
        { "Vary", "Accept-Encoding" },
        { "_eep-Alive", "timeout=45" },
        { "_onnection", "Keep-Alive" },
        { "Transfer-Encoding", "chunked" },
        { "Content-Type", "text/html" },
        { "Connection", "close" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest10)
{
    string responseRowString =
        "HTTP/1.1 500 Oriëntatieprobleem\r\n"
        "Date: Fri, 5 Nov 2010 23:07:12 GMT+2\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 500;
    response.headers = {
        { "Date", "Fri, 5 Nov 2010 23:07:12 GMT+2" }, { "Content-Length", "0" }, { "Connection", "close" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest11)
{
    string responseRowString =
        "HTTP/0.9 200 OK\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    ASSERT_TRUE(responses[0]->headers.size() == 0);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest12)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "hello world";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Content-Type", "text/plain" },
    };
    response.body = "hello world";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest13)
{
    string responseRowString =
        "HTTP/1.0 200 OK\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Connection", "keep-alive" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest14)
{
    string responseRowString =
        "HTTP/1.0 204 No content\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 204;
    response.headers = {
        { "Connection", "keep-alive" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest15)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    ASSERT_TRUE(responses[0]->headers.size() == 0);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest16)
{
    string responseRowString =
        "HTTP/1.1 204 No content\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 204;
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    ASSERT_TRUE(responses[0]->headers.size() == 0);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest17)
{
    string responseRowString =
        "HTTP/1.1 204 No content\r\n"
        "Connection: close\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 204;
    response.headers = {
        { "Connection", "close" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest18)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Transfer-Encoding", "chunked" },
    };
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest19)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Server: Microsoft-IIS/6.0\r\n"
        "X-Powered-By: ASP.NET\r\n"
        "en-US-Content-Type: text/xml\r\n"
        "Content-Type: text/xml\r\n"
        "Content-Length: 16\r\n"
        "Date: Fri, 23 Jul 2010 18:45:38 GMT\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "<xml>hello</xml>";
    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Server", "Microsoft-IIS/6.0" }, { "X-Powered-By", "ASP.NET" }, { "en-US-Content-Type", "text/xml" },
        { "Content-Type", "text/xml" },    { "Content-Length", "16" },    { "Date", "Fri, 23 Jul 2010 18:45:38 GMT" },
        { "Connection", "keep-alive" },
    };
    response.body = "<xml>hello</xml>";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest20)
{
    string responseRowString =
        "HTTP/1.1 301 MovedPermanently\r\n"
        "Date: Wed, 15 May 2013 17:06:33 GMT\r\n"
        "Server: Server\r\n"
        "x-amz-id-1: 0GPHKXSJQ826RK7GZEB2\r\n"
        "p3p: policyref=\"http://192.168.0.1:5000/www.amazon.com/w3c/p3p.xml\",CP=\"CAO DSP LAW CUR ADM IVAo IVDo "
        "CONo "
        "OTPo OUR DELi "
        "PUBi OTRi BUS PHY ONL UNI PUR FIN COM NAV INT DEM CNT STA HEA PRE LOC GOV OTC \"\r\n"
        "x-amz-id-2: STN69VZxIFSz9YJLbz1GDbxpbjG6Qjmmq5E3DxRhOUw+Et0p4hr7c/Q8qNcx4oAD\r\n"
        "Location: "
        "http://192.168.0.1:5000/www.amazon.com/Dan-Brown/e/B000AP9DSU/"
        "ref=s9_pop_gw_al1?_encoding=UTF8&refinementId=618073011&pf_rd_m=ATVPDKIKX0DER&pf_rd_s=center-2&pf_rd_r="
        "0SHYY5BZXN3KR20BNFAY&pf_rd_t=101&pf_rd_p=1263340922&pf_rd_i=507846\r\n"
        "Vary: Accept-Encoding,User-Agent\r\n"
        "Content-Type: text/html; charset=ISO-8859-1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "1\r\n"
        "\n\r\n"
        "0\r\n"
        "\r\n";
    HttpParseMessage response;
    response.statusCode = 301;
    response.headers = {
        { "Date", "Wed, 15 May 2013 17:06:33 GMT" },
        { "Server", "Server" },
        { "x-amz-id-1", "0GPHKXSJQ826RK7GZEB2" },
        { "p3p",
          "policyref=\"http://192.168.0.1:5000/www.amazon.com/w3c/p3p.xml\",CP=\"CAO DSP LAW CUR ADM IVAo IVDo CONo "
          "OTPo "
          "OUR DELi PUBi "
          "OTRi BUS PHY ONL UNI PUR FIN COM NAV INT DEM CNT STA HEA PRE LOC GOV OTC \"" },
        { "x-amz-id-2", "STN69VZxIFSz9YJLbz1GDbxpbjG6Qjmmq5E3DxRhOUw+Et0p4hr7c/Q8qNcx4oAD" },
        { "Location",
          "http://192.168.0.1:5000/www.amazon.com/Dan-Brown/e/B000AP9DSU/"
          "ref=s9_pop_gw_al1?_encoding=UTF8&refinementId=618073011&pf_rd_m=ATVPDKIKX0DER&pf_rd_s=center-2&pf_rd_r="
          "0SHYY5BZXN3KR20BNFAY&pf_rd_t=101&pf_rd_p=1263340922&pf_rd_i=507846" },
        { "Vary", "Accept-Encoding,User-Agent" },
        { "Content-Type", "text/html; charset=ISO-8859-1" },
        { "Transfer-Encoding", "chunked" },
    };
    response.body = "\n";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, ResponseTest21)
{
    string responseRowString =
        "HTTP/1.1 200 \r\n"
        "\r\n";

    HttpParseMessage response;
    response.statusCode = 200;
    response.body = "";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);

    ASSERT_TRUE(responses.size() == 1);
    BUSLOG_INFO("response size is: {}", responses[0]->headers.size());
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    ASSERT_TRUE(responses[0]->headers.size() == 0);
    EXPECT_EQ(responses[0]->body, response.body);
}

TEST(HttpParserTest, HttpReqDecoderHalf)
{
    string requestRowString1 =
        "POST /post_identity_body_world?q=search&page=123 HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: identity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "World"
        "POST /post_identity_body_world?q=search&page=123 HTTP/10.10\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: iden";

    string requestRowEmpty = "";

    string requestRowString2 =
        "tity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "22222";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.requestPath = "/post_identity_body_world";
    request.headers = {
        { "Accept", "*/*" }, { "Transfer-Encoding", "identity" }, { "Content-Length", "5" },
    };
    request.query = {
        { "q", "search" }, { "page", "123" },
    };
    request.body = "World";
    std::string body2 = "22222";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString1.data(), requestRowString1.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, request.body);
    for (unsigned i = 0; i < requests.size(); i++) {
        std::cout << "request url is: " << requests[i]->url << std::endl;
        std::cout << "request body is: " << requests[i]->body << std::endl;
        for (auto iter = requests[i]->headers.begin(); iter != requests[i]->headers.end(); ++iter) {
            std::cout << "request i:" << i << ", head=" << iter->first << ", value=" << iter->second << std::endl;
        }
        delete requests[i];
    }

    requests.clear();
    requests = decoder->Decode(requestRowEmpty.data(), requestRowEmpty.length());
    ASSERT_TRUE(decoder->Failed() == false);
    ASSERT_TRUE(requests.size() == 0);

    requests = decoder->Decode(requestRowString2.data(), requestRowString2.length());
    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(requests[0]->method, "POST");
    EXPECT_EQ(requests[0]->keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requests[0]->headers, request.headers);
    EXPECT_EQ(requests[0]->url.path, request.requestPath);
    EXPECT_EQ(requests[0]->url.query, request.query);
    EXPECT_EQ(requests[0]->body, body2);
    for (unsigned i = 0; i < requests.size(); i++) {
        std::cout << "request url is: " << requests[i]->url << std::endl;
        std::cout << "request body is: " << requests[i]->body << std::endl;
        for (auto iter = requests[i]->headers.begin(); iter != requests[i]->headers.end(); ++iter) {
            std::cout << "request i:" << i << ", head=" << iter->first << ", value=" << iter->second << std::endl;
        }
        delete requests[i];
    }
}

TEST(HttpParserTest, HttpReqDecoderByChar)
{
    string requestRowString =
        "POST /post_identity_body_world?q=search&page=123 HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: identity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "World"
        "POST http://192.168.0.1:5000/post_identity_body_world?q=search&page=123 HTTP/10.10\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: identity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "22222";

    HttpParseMessage request;
    request.shouldKeepAlive = true;
    request.scheme = "http";
    request.host = "192.168.0.1";
    request.port = 5000;
    request.requestPath = "/post_identity_body_world";
    request.headers = {
        { "Accept", "*/*" }, { "Transfer-Encoding", "identity" }, { "Content-Length", "5" },
    };
    request.query = {
        { "q", "search" }, { "page", "123" },
    };
    request.body = "World";
    std::string body2 = "22222";

    RequestDecoder *decoder = new RequestDecoder();
    string s = requestRowString.substr(0, 1);
    deque<Request *> requests;
    deque<Request> requestsBak;
    requests = decoder->Decode(s.c_str(), 1);
    if (!requests.empty()) {
        if (!requests.empty()) {
            for (unsigned i = 0; i < requests.size(); i++) {
                requestsBak.push_back(*(requests[i]));
                delete requests[i];
            }
            requests.clear();
        }
    }

    for (unsigned int i = 1; i < requestRowString.length(); ++i) {
        s = requestRowString.substr(i, i + 1);
        requests = decoder->Decode(s.c_str(), 1);
        if (!requests.empty()) {
            if (!requests.empty()) {
                for (unsigned i = 0; i < requests.size(); i++) {
                    requestsBak.push_back(*(requests[i]));
                    delete requests[i];
                }
                requests.clear();
            }
        }
    }
    BUSLOG_INFO("request size is: {}", requestsBak.size());
    ASSERT_TRUE(requestsBak.size() == 2);
    EXPECT_EQ(requestsBak[0].method, "POST");
    EXPECT_EQ(requestsBak[0].keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requestsBak[0].headers, request.headers);
    EXPECT_EQ(requestsBak[0].url.path, request.requestPath);
    EXPECT_EQ(requestsBak[0].url.query, request.query);
    EXPECT_EQ(requestsBak[0].body, request.body);
    EXPECT_EQ(requestsBak[1].method, "POST");
    EXPECT_EQ(requestsBak[1].keepAlive, request.shouldKeepAlive);
    EXPECT_EQ(requestsBak[1].headers, request.headers);
    EXPECT_EQ(requestsBak[1].url.scheme.Get(), request.scheme);
    EXPECT_EQ(requestsBak[1].url.ip.Get(), request.host);
    EXPECT_EQ(requestsBak[1].url.port.Get(), request.port);
    EXPECT_EQ(requestsBak[1].url.path, request.requestPath);
    EXPECT_EQ(requestsBak[1].url.query, request.query);
    EXPECT_EQ(requestsBak[1].body, body2);

    for (unsigned i = 0; i < requestsBak.size(); i++) {
        std::cout << "request url is: " << requestsBak[i].url << std::endl;
        std::cout << "request body is: " << requestsBak[i].body << std::endl;
        for (auto iter = requestsBak[i].headers.begin(); iter != requestsBak[i].headers.end(); ++iter) {
            std::cout << "request i:" << i << ", head=" << iter->first << ", value=" << iter->second << std::endl;
        }
    }
    requestsBak.clear();
}

TEST(HttpParserTest, HttpRspDecoderByChar)
{
    string responseRowString =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: 11\r\n"
        "Proxy-Connection: close\r\n"
        "Date: Thu, 31 Dec 2009 20:55:48 +0000\r\n"
        "\r\n"
        "hello world"
        "HTTP/1.1 200 OK\r\n"
        "Server: Microsoft-IIS/6.0\r\n"
        "X-Powered-By: ASP.NET\r\n"
        "en-US-Content-Type: text/xml\r\n"
        "Content-Type: text/xml\r\n"
        "Content-Length: 16\r\n"
        "Date: Fri, 23 Jul 2010 18:45:38 GMT\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "<xml>hello</xml>";

    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Server", "Microsoft-IIS/6.0" }, { "X-Powered-By", "ASP.NET" }, { "en-US-Content-Type", "text/xml" },
        { "Content-Type", "text/xml" },    { "Content-Length", "16" },    { "Date", "Fri, 23 Jul 2010 18:45:38 GMT" },
        { "Connection", "keep-alive" },
    };
    response.body = "<xml>hello</xml>";

    ResponseDecoder *decoder = new ResponseDecoder();
    string s = responseRowString.substr(0, 1);
    deque<http::Response *> responses = decoder->Decode(s.c_str(), 1);
    for (unsigned int i = 1; i < responseRowString.length(); ++i) {
        s = responseRowString.substr(i, i + 1);
        responses = decoder->Decode(s.c_str(), 1);
    }
    BUSLOG_INFO("response size is: {}", responses.size());
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, response.body);

    for (unsigned i = 0; i < responses.size(); i++) {
        BUSLOG_INFO("responses retCode is: {}", responses[i]->retCode);
        BUSLOG_INFO("responses body is: {}", responses[i]->body);
        for (auto iter = responses[i]->headers.begin(); iter != responses[i]->headers.end(); ++iter) {
            BUSLOG_INFO("response i: {}, head={}, value={}", i, iter->first, iter->second);
        }
        delete responses[i];
    }

    responses.clear();
}

TEST(HttpParserTest, ResponseWithUnspecifiedLength)
{
    string responseRowString1 =
        "HTTP/1.1 200 OK\r\n"
        "Date: Tue, 04 Aug 2009 07:59:32 GMT\r\n"
        "Server: Apache\r\n"
        "X-Powered-By: Servlet/2.5 JSP/2.1\r\n"
        "Content-Type: text/xml; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        "  <SOAP-ENV:Body>\n"
        "    <SOAP-ENV:Fault>\n"
        "       <faultcode>SOAP-ENV:Client</faultcode>\n"
        "       <faultstring>Client Error</faultstring>\n"
        "    </SOAP-ENV:Fault>\n"
        "  </SOAP-ENV:Body>\n"
        "</SOAP-ENV:Envelope>";

    string body1 =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        "  <SOAP-ENV:Body>\n"
        "    <SOAP-ENV:Fault>\n"
        "       <faultcode>SOAP-ENV:Client</faultcode>\n"
        "       <faultstring>Client Error</faultstring>\n"
        "    </SOAP-ENV:Fault>\n"
        "  </SOAP-ENV:Body>\n"
        "</SOAP-ENV:Envelope>";

    string responseRowString2 =
        "HTTP/1.1 200 OK\r\n"
        "Content-type: application/JSON;CHARSET=UTF-8\r\n\r\n"
        "\r\n"
        "these headers are from http://news.ycombinator.com/";
    string body2 = "these headers are from http://news.ycombinator.com/";

    HttpParseMessage response;
    response.statusCode = 200;
    response.headers = {
        { "Date", "Tue, 04 Aug 2009 07:59:32 GMT" },
        { "Server", "Apache" },
        { "X-Powered-By", "Servlet/2.5 JSP/2.1" },
        { "Content-Type", "text/xml; charset=utf-8" },
        { "Connection", "close" },
    };

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString1.data(), responseRowString1.length());

    BUSLOG_INFO("response size is: {}", responses.size());
    ASSERT_TRUE(responses.empty());

    responses = decoder->Decode("", 0);
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, body1);
    for (unsigned i = 0; i < responses.size(); i++) {
        BUSLOG_INFO("responses retCode is: {}", responses[i]->retCode);
        BUSLOG_INFO("responses body is: {}", responses[i]->body);
        for (auto iter = responses[i]->headers.begin(); iter != responses[i]->headers.end(); ++iter) {
            BUSLOG_INFO("response i: {}, head={}, value={}", i, iter->first, iter->second);
        }
        delete responses[i];
    }
    responses.clear();
    response.headers.clear();
    response.headers = {
        { "Content-type", "application/JSON;CHARSET=UTF-8" },
    };

    responses = decoder->Decode(responseRowString2.data(), responseRowString2.length());
    ASSERT_TRUE(responses.empty());
    responses = decoder->Decode("", 0);
    ASSERT_TRUE(responses.size() == 1);
    EXPECT_EQ(responses[0]->retCode, response.statusCode);
    EXPECT_EQ(responses[0]->headers, response.headers);
    EXPECT_EQ(responses[0]->body, body2);
    for (unsigned i = 0; i < responses.size(); i++) {
        BUSLOG_INFO("responses retCode is: {}", responses[i]->retCode);
        BUSLOG_INFO("responses body is: {}", responses[i]->body);
        for (auto iter = responses[i]->headers.begin(); iter != responses[i]->headers.end(); ++iter) {
            BUSLOG_INFO("response i: {}, head={}, value={}", i, iter->first, iter->second);
        }
        delete responses[i];
    }
    responses.clear();
}

TEST(HttpParserTest, ResponseEmpty)
{
    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode("", 0, 1);

    ASSERT_TRUE(responses.empty());
    EXPECT_EQ(decoder->Failed(), false);
}

TEST(HttpParserTest, ParseBodyUpgrade)
{
    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode("", 0, 1);
    char ch = 'x';
    decoder->ParseBodyUpgrade(ch);
    ASSERT_TRUE(responses.empty());
    EXPECT_EQ(decoder->Failed(), false);
}
TEST(HttpParserTest, ParseBodyOthers)
{
    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode("", 0, 1);
    char ch = 'x';
    decoder->ParseBodyOthers(ch);
    ASSERT_TRUE(responses.empty());
    EXPECT_EQ(decoder->Failed(), false);
}

TEST(HttpParserTest, RequestBigUrl)
{
    std::string urlPath = string(1024, 'a');

    string requestRowString = std::string() + "GET http://192.168.0.1:5000/" + urlPath + " HTTP/1.1\r\n\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 1);
    EXPECT_EQ(decoder->Failed(), false);

    std::string bigUrlPath = string(2048 + 1, 'a');
    std::string bigUrlRowString = std::string() + "GET http://192.168.0.1:5000/" + bigUrlPath + " HTTP/1.1\r\n\r\n";
    RequestDecoder *bigUrlDecoder = new RequestDecoder();
    deque<Request *> bigUrlRequests = bigUrlDecoder->Decode(bigUrlRowString.data(), bigUrlRowString.length());

    ASSERT_TRUE(bigUrlRequests.size() == 0);
    EXPECT_EQ(bigUrlDecoder->Failed(), true);
    EXPECT_EQ(bigUrlDecoder->GetErrorCode(), HTTP_INVALID_URL_LENGTH);
}

TEST(HttpParserTest, RequestBigSizeUrl)
{
    std::string urlPath = string(8024, 'a');

    string requestRowString = std::string() + "GET http://192.168.0.1:5000/" + urlPath;

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    EXPECT_EQ(decoder->Failed(), true);
}

TEST(HttpParserTest, RequestBigHeaderField)
{
    std::string headerField = string(8024 + 1, 'a');

    string requestRowString = std::string() + "POST http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
                              + headerField + ": bbbbb\r\n\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
    EXPECT_EQ(decoder->GetErrorCode(), HTTP_INVALID_FIELD_LENGTH);
}

TEST(HttpParserTest, RequestBigSizeHeaderField)
{
    std::string headerField = string(8024 + 1, 'a');

    string requestRowString = std::string() + "POST http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
                              + headerField + ": bbbbb";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
    EXPECT_EQ(decoder->GetErrorCode(), HTTP_INVALID_FIELD_LENGTH);
}

TEST(HttpParserTest, RequestBigHeaderValue)
{
    std::string headerValue = string(65536 + 1, 'a');

    string requestRowString = std::string() + "POST http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
                              + "aaaaa:" + headerValue + "\r\n\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
}

TEST(HttpParserTest, RequestBigSizeHeaderValue)
{
    std::string headerValue = string(65536 + 1, 'a');

    string requestRowString = std::string() + "POST http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
                              + "aaaaa:" + headerValue;

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
}

TEST(HttpParserTest, RequestBigHeaderSize)
{
    std::string headerValue = "a:b\r\n";

    string requestRowString = std::string() + "POST http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n";

    for (int i = 0; i < 1024 + 1; i++) {
        requestRowString += headerValue;
    }

    requestRowString += "\r\n";

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
    EXPECT_EQ(decoder->GetErrorCode(), HTTP_INVALID_HEADER_NUM);
}

TEST(HttpParserTest, RequestBigBody)
{
    string requestRowString = std::string() + "GET http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
                              + "content-Length: 104857601\r\n\r\n";

    std::string body = string(104857600 + 1, 'a');

    requestRowString += body;

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
}

TEST(HttpParserTest, RequestBigSizeBody)
{
    string requestRowString = std::string() + "GET http://192.168.0.1:5000/post_chunked_all_your_base HTTP/1.1\r\n"
                              + "content-Length: 104857601\r\n";

    std::string body = string(20971520 + 1, 'a');

    requestRowString += body;

    RequestDecoder *decoder = new RequestDecoder();
    deque<Request *> requests = decoder->Decode(requestRowString.data(), requestRowString.length());

    ASSERT_TRUE(requests.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
}

TEST(HttpParserTest, ResponseBigHeaderField)
{
    std::string headerField = string(1024 + 1, 'a');

    string responseRowString = std::string() + "HTTP/1.1 200 OK\r\n" + headerField + ": bbbbb\r\n\r\n";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
    EXPECT_EQ(decoder->GetErrorCode(), HTTP_INVALID_FIELD_LENGTH);
}

TEST(HttpParserTest, ResponseBigHeaderValue)
{
    std::string headerValue = string(65536 + 1, 'a');

    string responseRowString = std::string() + "HTTP/1.1 200 OK\r\n" + "aaaaa:" + headerValue + "\r\n\r\n";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
    EXPECT_EQ(decoder->GetErrorCode(), HTTP_INVALID_VALUE_LENGTH);
}

TEST(HttpParserTest, ResponseBigHeaderSize)
{
    std::string headerValue = "a:b\r\n";

    string responseRowString = std::string() + "HTTP/1.1 200 OK\r\n";

    for (int i = 0; i < 1024 + 1; i++) {
        responseRowString += headerValue;
    }

    responseRowString += "\r\n";

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
    EXPECT_EQ(decoder->GetErrorCode(), HTTP_INVALID_HEADER_NUM);
}

TEST(HttpParserTest, ResponseBigBody)
{
    string responseRowString = std::string() + "HTTP/1.1 200 OK\r\n" + "content-Length: 104857601\r\n\r\n";

    std::string body = string(20971520 + 1, 'a');

    responseRowString += body;

    ResponseDecoder *decoder = new ResponseDecoder();
    deque<http::Response *> responses = decoder->Decode(responseRowString.data(), responseRowString.length());

    ASSERT_TRUE(responses.size() == 0);
    EXPECT_EQ(decoder->Failed(), true);
}
