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

#include <climits>
#include <sstream>
#include "actor/buslog.hpp"
#include "iomgr/socket_operate.hpp"
#include "litebus.hpp"
#include "httpd/http.hpp"

#include "async/status.hpp"
#include "async/try.hpp"
#include "utils/string_utils.hpp"

using namespace std;

namespace litebus {
namespace http {
bool ParseDomainUrl(const std::string &urlFrom, string &schemeString, string &hostData, uint16_t &portData,
                    string &pathString)
{
    size_t schemeIndex = urlFrom.find("://");
    if (schemeIndex == string::npos) {
        return false;
    }

    schemeString = urlFrom.substr(0, schemeIndex);
    if (schemeString != HTTP_SCHEME && schemeString != HTTPS_SCHEME) {
        BUSLOG_ERROR("illegal scheme: {}", schemeString);
        return false;
    }

    const size_t schemeDelimiterSize = 3;
    const string fullPath = urlFrom.substr(schemeIndex + schemeDelimiterSize);

    size_t pathIndex = fullPath.find_first_of('/');
    if (pathIndex == std::string::npos) {
        BUSLOG_ERROR("not found path from {}", fullPath);
        return false;
    }

    pathString = fullPath.substr(pathIndex);

    string hostString = fullPath.substr(0, pathIndex);
    if (hostString.empty()) {
        BUSLOG_ERROR("host is empty!");
        return false;
    }

    if (hostString.rfind(":") == std::string::npos) {
        BUSLOG_INFO("host append default port");
        if (schemeString == HTTPS_SCHEME) {
            hostString.append(":443");
        } else {
            hostString.append(":80");
        }
    }

    portData = SocketOperate::GetPort(hostString);
    if (portData == 0) {
        BUSLOG_ERROR("illegal port!");
        return false;
    }

    hostData = SocketOperate::GetIP(hostString);
    if (hostData.empty()) {
        BUSLOG_ERROR("unreachable host");
        return false;
    }

    return true;
}

Try<URL> URL::Decode(const std::string &urlFrom, bool domainDecode)
{
    string schemeString;
    string hostData;
    uint16_t portData;
    string pathString;

    if (domainDecode) {
        if (!ParseDomainUrl(urlFrom, schemeString, hostData, portData, pathString)) {
            return Failure(Status::KERROR);
        }
    } else {
        if (urlFrom.find("/") != 0) {
            return Failure(Status::KERROR);
        }

        // NOTE : use litebus ip and port instead
        schemeString = litebus::GetLitebusAddress().scheme;
        hostData = litebus::GetLitebusAddress().ip;
        portData = litebus::GetLitebusAddress().port;
        pathString = urlFrom;
    }

    // handle path
    string pathData;
    Try<pair<unordered_map<string, string>, unordered_map<string, vector<string>>>> queryData;
    size_t queryIndex = pathString.find_first_of('?');
    if (queryIndex != std::string::npos) {
        pathData = pathString.substr(0, queryIndex);
        string queryString = pathString.substr(queryIndex + 1);
        // handle query
        queryData = litebus::http::query::Decode(queryString);
    } else {
        pathData = pathString;
    }

    if (queryData.IsOK()) {
        return URL(schemeString, hostData, portData, pathData, queryData.Get().first, queryData.Get().second);
    } else {
        return URL(schemeString, hostData, portData, pathData);
    }
}

Try<string> Decode(const string &queryString)
{
    std::ostringstream queryStream;
    size_t index = 0;
    while (index < queryString.length()) {
        if (queryString[index] == '%') {
            // we expect two Hexadecimal number behind '%', as '%XX'
            const size_t hexShiftFirst = 1;
            const size_t hexShiftLast = 2;
            if (index + hexShiftLast >= queryString.length()) {
                BUSLOG_WARN("decode query failed, query string:{}", queryString);
                return Failure(Status::KERROR);
            }

            if (!isxdigit(queryString[index + hexShiftFirst]) || !isxdigit(queryString[index + hexShiftLast])) {
                BUSLOG_WARN("decode query failed, query string:{}", queryString);
                return Failure(Status::KERROR);
            }

            // convert from '%XX' to char
            unsigned long hexValue;
            istringstream hexStream(queryString.substr(index + hexShiftFirst, hexShiftLast));
            hexStream >> std::hex >> hexValue;
            if (hexValue > UCHAR_MAX) {
                BUSLOG_WARN("decode query failed, query string:{}", queryString);
                return Failure(Status::KERROR);
            }

            queryStream << static_cast<unsigned char>(hexValue);
            index += hexShiftLast;
        } else if (queryString[index] == '+') {
            // convert from '+' to ' '
            queryStream << ' ';
        } else {
            queryStream << queryString[index];
        }
        index++;
    }
    return queryStream.str();
}

namespace query {
QueriesTry Decode(const string &query)
{
    unordered_map<string, string> queryMap;
    unordered_map<string, vector<string>> rawQueryMap;
    const vector<string> tokens = strings::Tokenize(query, ",&");
    vector<string>::const_iterator iter;
    for (iter = tokens.begin(); iter != tokens.end(); ++iter) {
        const vector<string> queryPairs = strings::Split(*iter, "=");
        if (queryPairs.size() > 0) {
            // begin to decode query field and query value
            Try<string> field = http::Decode(queryPairs[0]);
            if (field.IsError()) {
                return Failure(Status::KERROR);
            }

            if (queryPairs.size() == 1) {
                // by default, value is empty string
                queryMap[field.Get()] = "";
                rawQueryMap[field.Get()].emplace_back("");
            } else {
                Try<string> value = http::Decode(queryPairs[1]);
                if (value.IsError()) {
                    return Failure(Status::KERROR);
                }

                BUSLOG_DEBUG("decode query, key:{},value:{}", field.Get(), value.Get());
                queryMap[field.Get()] = value.Get();
                rawQueryMap[field.Get()].emplace_back(value.Get());
            }
        }
    }
    return std::make_pair(queryMap, rawQueryMap);
}
}    // namespace query
}    // namespace http
};    // namespace litebus
