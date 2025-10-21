#include <signal.h>

#include "actor/buslog.hpp"
#include "async/async.hpp"
#include "litebus.hpp"

#include <memory>

#include "async/future.hpp"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "async/option.hpp"

#include <async/flag_parser_impl.hpp>
#include <async/flag_parser.hpp>
#include "ssl/openssl_wrapper.hpp"

using namespace std;
using namespace litebus;

const std::string g_client_name("Litebus_Client");
// using litebus::http
using litebus::http::HttpConnect;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::URL;

class LitebusClient : public litebus::ActorBase {
public:
    LitebusClient(std::string name) : ActorBase(name)
    {
    }

    ~LitebusClient()
    {
    }

private:
    virtual void Init() override
    {
        BUSLOG_INFO("init LiteBus_Server...");
    }

    void handleAck(litebus::AID from, std::string &&type, std::string &&data)
    {
        BUSLOG_INFO("ack received");
    }

    void Exited(const litebus::AID &from)
    {
        BUSLOG_INFO("server has crashed, from= {}", from);
    }
};

const string SHEME_HTTPS = "https";

class Flags : public litebus::flag::FlagParser {
public:
    Flags();
    ~Flags()
    {
    }
    int descryptType = 1;    // -1:INVIREMENT;0:None;1:OSS_DECRYPT;2:moca;100:default;
    string sslSandBox = "";
    string url;
    string httpMethod = "GET";
    uint32_t timeout = 90;
    string contentType = "application/json";

    Option<string> body;
};

namespace litebus {
static std::string GetCWD()
{
    size_t size = 100;

    while (true) {
        char *temp = new char[size];
        if (::getcwd(temp, size) == temp) {
            std::string result(temp);
            delete[] temp;
            return result;
        } else {
            if (errno != ERANGE) {
                delete[] temp;
                return std::string();
            }
            size *= 2;
            delete[] temp;
        }
    }

    return std::string();
}

void SetLitebusHttpsTestEnv(int type, bool sslInitRet = true,
                            std::string rootStandardizd = "", std::string comStandardizd = "",
                            std::string dpkeyStandardizd = "", std::string dpdirStandardizd = "")
{
    const char *sslSandBox = getenv("LITEBUS_SSL_SANDBOX");

    std::map<std::string, std::string> environment;

    switch (type) {
        case 0:
            environment["LITEBUS_SSL_ENABLED"] = "0";
            break;
        case 100: {
            std::string keyPath = std::string(sslSandBox) + "default_keys/server.key";
            std::string certPath = std::string(sslSandBox) + "default_keys/server.crt";
            BUSLOG_INFO("keyPath is {}", keyPath);
            BUSLOG_INFO("certPath is {}", certPath);
            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_KEY_FILE"] = keyPath;
            environment["LITEBUS_SSL_CERT_FILE"] = certPath;
            break;
        }

        case 1: {
            std::string keyPath = std::string(sslSandBox) + "CSPEdge.Enc.pem.key";
            std::string certPath = std::string(sslSandBox) + "CSPEdge.pem.cer";
            std::string rootCertPath = std::string(sslSandBox) + "CA.crt";
            std::string rootCertDirPath = std::string(sslSandBox);
            std::string decryptPath = std::string(sslSandBox);
            std::string decryptRootPath = std::string(sslSandBox) + "root.key";
            std::string decryptCommonPath = std::string(sslSandBox) + "common_shared.key";
            std::string decryptKeyPath = std::string(sslSandBox) + "ICTS_CCN.Enc.key.pwd";

            BUSLOG_INFO("keyPath is {}", keyPath);
            BUSLOG_INFO("certPath is {}", certPath);
            BUSLOG_INFO("rootCertPath is {}", rootCertPath);
            BUSLOG_INFO("decryptPath is {}", decryptPath);
            BUSLOG_INFO("decryptRootPath is {}", decryptRootPath);
            BUSLOG_INFO("decryptCommonPath is {}", decryptCommonPath);
            BUSLOG_INFO("decryptKeyPath is {}", decryptKeyPath);

            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_KEY_FILE"] = keyPath;
            environment["LITEBUS_SSL_CERT_FILE"] = certPath;
            environment["LITEBUS_SSL_REQUIRE_CERT"] = "1";
            environment["LITEBUS_SSL_VERIFY_CERT"] = "1";
            environment["LITEBUS_SSL_CA_DIR"] = rootCertDirPath;
            environment["LITEBUS_SSL_CA_FILE"] = rootCertPath;
            environment["LITEBUS_SSL_DECRYPT_TYPE"] = "1";
            environment["LITEBUS_SSL_DECRYPT_DIR"] = decryptPath;
            environment["LITEBUS_SSL_DECRYPT_ROOT_FILE"] = decryptRootPath;
            environment["LITEBUS_SSL_DECRYPT_COMMON_FILE"] = decryptCommonPath;
            environment["LITEBUS_SSL_DECRYPT_KEY_FILE"] = decryptKeyPath;
            break;
        }
        case 2: {
            std::string keyPath = std::string(sslSandBox) + "moca_keys/MSP_File";
            std::string certPath = std::string(sslSandBox) + "moca_keys/MSP.pem.cer";
            std::string rootCertPath = std::string(sslSandBox) + "moca_keys/CA.pem.cer";
            std::string rootCertDirPath = std::string(sslSandBox) + "moca_keys/";
            std::string decryptPath = std::string(sslSandBox) + dpdirStandardizd + "moca_keys/ct/";

            BUSLOG_INFO("keyPath is {}", keyPath);
            BUSLOG_INFO("certPath is {}", certPath);
            BUSLOG_INFO("rootCertPath is {}", rootCertPath);
            BUSLOG_INFO("decryptPath is {}", decryptPath);

            environment["LITEBUS_SSL_ENABLED"] = "1";
            environment["LITEBUS_SSL_KEY_FILE"] = keyPath;
            environment["LITEBUS_SSL_CERT_FILE"] = certPath;
            environment["LITEBUS_SSL_REQUIRE_CERT"] = "1";
            environment["LITEBUS_SSL_VERIFY_CERT"] = "1";
            environment["LITEBUS_SSL_CA_DIR"] = rootCertDirPath;
            environment["LITEBUS_SSL_CA_FILE"] = rootCertPath;
            environment["LITEBUS_SSL_DECRYPT_TYPE"] = "2";
            environment["LITEBUS_SSL_DECRYPT_DIR"] = decryptPath;
            break;
        }
    }

    FetchSSLConfigFromMap(environment);
}

int HTTPPost(const URL &url, string contentType, string req)
{
    litebus::Future<Response> response;
    std::string reqData = req;
    response = litebus::http::Post(url, None(), reqData, contentType);
    BUSLOG_INFO("response: {}", response.Get().body);
    int code = response.Get().retCode;
    return code;
}

int HTTPGet(const URL &url, string contentType)
{
    litebus::Future<Response> response;
    std::unordered_map<std::string, std::string> headers;
    headers["Connection"] = "close";
    response = litebus::http::Get(url, headers, litebus::None());
    BUSLOG_INFO("response: {}", response.Get().body);
    int code = response.Get().retCode;
    return code;
}

Future<Response> sendRequest(string method, const URL &url,
                             const Option<std::unordered_map<std::string, std::string>> &headers,
                             const Option<std::string> &body, const Option<std::string> &contentType,
                             const Option<uint64_t> &reqTimeout)
{
    if (body.IsNone() && contentType.IsSome()) {
        BUSLOG_WARN("Couldn't create post request with a content-type but no body");
        return Status(-10);
    }

    Request request(method, false, url);

    if (headers.IsSome()) {
        for (const auto &headerItem : headers.Get()) {
            request.headers[headerItem.first] = headerItem.second;
        }
    }

    if (body.IsSome()) {
        request.body = body.Get();
    }

    if (contentType.IsSome()) {
        request.headers["Content-Type"] = contentType.Get();
    }

    if (reqTimeout.IsSome()) {
        request.timeout = reqTimeout.Get();
    } else {
        request.timeout = 90000;
    }

    return LaunchRequest(request);
}

bool CheckReqType(const std::string &method)
{
    return (litebus::http::ALLOW_METHOD.find(method) != litebus::http::ALLOW_METHOD.end());
}

// read from a file
Try<std::string> ReadFile(const std::string &path)
{
    FILE *file = ::fopen(path.c_str(), "r");
    if (file == nullptr) {
        BUSLOG_WARN("can not open  file: {}", path);
        return Try<std::string>(-1);
    }

    // Use a buffer to read the file in BUFSIZ
    // chunks and append it to the string we return.
    //
    // NOTE: We aren't able to use fseek() / ftell() here
    // to find the file size because these functions don't
    // work properly for in-memory files like /proc/*/stat.
    char *buffer = new char[BUFSIZ];
    std::string result;

    while (true) {
        size_t read = ::fread(buffer, 1, BUFSIZ, file);

        if (::ferror(file)) {
            // NOTE: ferror() will not modify errno if the stream
            // is valid, which is the case here since it is open.
            delete[] buffer;
            ::fclose(file);
            return Try<std::string>(-1);
        }

        result.append(buffer, read);

        if (read != BUFSIZ) {
            break;
        }
    };

    ::fclose(file);
    delete[] buffer;
    BUSLOG_WARN("read result: {}", result);
    return result;
}

Option<string> GetBodyFromInput(Option<string> input)
{
    // json is primary
    Option<string> body = None();
    if (input.IsSome()) {
        string jsonString = input.Get();
        char path[PATH_MAX + 1] = { 0x00 };
        if (strlen(jsonString.c_str()) > PATH_MAX || nullptr == realpath(jsonString.c_str(), path)) {
            BUSLOG_WARN("not a json file, process as json: {}", jsonString);
            body = jsonString;
        } else {
            Try<string> jsonfileString = ReadFile(string(path));
            if (jsonfileString.IsOK()) {
                body = jsonfileString.Get();
            }
        }
    }
    return body;
}

}    // namespace litebus

Flags::Flags()
{
    AddFlag(&Flags::url, "url", "MUST BE SET, url, formate:https://ip:port/url");

    AddFlag(&Flags::httpMethod, "method", "httpMethod:GET/POST/PUT/DELETE", "GET");

    AddFlag(&Flags::descryptType, "decrypt",
            "DescryptType,-1;by inviroment, WITHOUT_DECRYPT = 0, OSS_DECRYPT = 1, HARES_DECRYPT = 2, UNKNOWN_DECRYPT "
            "= 100",
            1);

    AddFlag(&Flags::sslSandBox, "sslpath", "sslSandBox configuration json ", litebus::GetCWD());

    AddFlag(&Flags::body, "body", "body string or file, optional, this is primary input for json");

    AddFlag(&Flags::timeout, "timeout", "timeout to request(second)", 90);

    AddFlag(&Flags::contentType, "contenttype",
            "request header content type, default is 'application/json', 'text/html'", "application/json");
};

int main(int argc, char **argv)
{
    BUSLOG_INFO("star client .....");
    Flags flags;
    Option<string> loadErr = flags.ParseFlags(argc, argv);
    if (loadErr.IsSome()) {
        BUSLOG_INFO(loadErr.Get());
        exit(-1);
    }
    BUSLOG_INFO("decrypt type: {}", flags.descryptType);

    // by sslpath, <0 user should set envirement var
    if (flags.descryptType > 0) {
        setenv("LITEBUS_SSL_SANDBOX", flags.sslSandBox.c_str(), true);
        litebus::SetLitebusHttpsTestEnv(flags.descryptType);
    }

    litebus::Initialize("");
    Try<URL> requestUrl = URL::Decode(flags.url);
    std::string contentType = flags.contentType;

    Option<string> body = GetBodyFromInput(flags.body);

    BUSLOG_ERRPR("contentType: {}", contentType);
    BUSLOG_ERROR("http method: {}", flags.httpMethod);
    BUSLOG_ERROR("htttp decrypt type: {}", flags.descryptType);
    BUSLOG_ERROR("http ssl path: {}", flags.sslSandBox.c_str());

    if (body.IsSome()) {
        BUGLOG_ERROR("http body: {}", body.Get());
    }

    if (litebus::CheckReqType(flags.httpMethod)) {
        if (flags.httpMethod == "GET") {
            HTTPGet(requestUrl.Get(), contentType);
        } else if (flags.httpMethod == "POST") {
            HTTPPost(requestUrl.Get(), contentType, body.IsSome() ? body.Get() : "");
        } else {
            Future<Response> response;

            std::unordered_map<std::string, std::string> headers;
            headers["Connection"] = "close";

            BUGLOG_INFO("will send request: {}, body: {}", flags.httpMethod, body.Get());
            uint32_t requestTimeout = flags.timeout * 1000;
            response = sendRequest(flags.httpMethod, requestUrl.Get(), headers, body.IsSome() ? body.Get() : "",
                                   contentType, requestTimeout);
            BUSLOG_INFO("end send request {}, response body is: {}", flags.httpMethod, response.Get().body);
        }
    } else {
        BUSLOG_ERROR("ERROR http method");
    }
}
