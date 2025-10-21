#include "httpd/http.hpp"
#include "httpd/http_parser.hpp"
#include "httpd/http_decoder.hpp"

#include <gtest/gtest.h>

using namespace std;
using namespace litebus;
using namespace litebus::http;

TEST(HttpDecoderTest, HttpDecoderTestHalfUrl)
{
    string requestRowString1 =
        "POST /post_identity_body_world?q=search&page=123 HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: identity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "World"
        "POST /post_ide";

    string requestRowString2 =
        "ntity_body_world?q=search&page=123 HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Transfer-Encoding: identity\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "World";

    RequestDecoder *decoder = new RequestDecoder();

    deque<Request *> requests = decoder->Decode(requestRowString1.data(), requestRowString1.length());

    ASSERT_TRUE(requests.size() == 1);

    for (unsigned i = 0; i < requests.size(); i++) {
        BUSLOG_INFO("request url is: {}", requests[i]->url);
        BUSLOG_INFO("request body is: {}", requests[i]->body);
        delete requests[i];
    }

    requests.clear();

    requests = decoder->Decode(requestRowString2.data(), requestRowString2.length());
    ASSERT_TRUE(requests.size() == 1);

    for (unsigned i = 0; i < requests.size(); i++) {
        BUSLOG_INFO("request url is: {}", requests[i]->url);
        BUSLOG_INFO("request body is: {}", requests[i]->body);
        delete requests[i];
    }
}
