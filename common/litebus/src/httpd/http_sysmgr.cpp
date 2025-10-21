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

#include "httpd/http_sysmgr.hpp"
#include "actor/buslog.hpp"
#include "async/asyncafter.hpp"
#include "async/future.hpp"
#include "httpd/http_actor.hpp"

using namespace std;

namespace litebus {
namespace http {

const int DURATION_MAX = 24 * 3600 * 1000;

Try<int> StrToInt(std::string &str)
{
    try {
        return std::stoi(str);
    } catch (std::exception &e) {
        return Failure(Status::KERROR);
    }
}

Future<Response> HttpSysMgr::VlogToggle(const http::Request &request)
{
    BUSLOG_INFO("url,method,client,body size, u:{},m:{},c:{},s:{}", request.url.path, request.method,
                request.client.Get(), request.body.size());
    if (request.method != "POST") {
        return BadRequest("Invalid method '" + request.method + "'.\n");
    }
    auto iterLevel = request.url.query.find("level");
    auto iterDuration = request.url.query.find("duration");
    if ((iterLevel == request.url.query.end()) || (iterDuration == request.url.query.end())) {
        return Ok("level or duration is null. orgLevel=" + std::to_string(orgLevel) + "\n");
    }
    std::string level = iterLevel->second;
    std::string duration = iterDuration->second;

    Try<int> v = StrToInt(level);
    if (v.IsError()) {
        return BadRequest(v.GetErrorCode() + ".\n");
    }

    // if level is not '0' but convert to 0, also consider as fail
    bool invalidLevelStr = (level != "0" && v.Get() == 0);
    if (v.Get() < 0 || invalidLevelStr) {
        return BadRequest("Invalid level '" + level + "'.\n");
    } else if (v.Get() < orgLevel) {
        return BadRequest("'" + level + "' < orgLevel level.\n");
    }

    Try<int> d = StrToInt(duration);
    if (d.IsError()) {
        return BadRequest(d.GetErrorCode() + ".\n");
    }

    // duration should between 0 and max duration
    if (d.Get() <= 0 || d.Get() > DURATION_MAX) {
        return BadRequest("Invalid duration '" + duration + "'.\n");
    }

    BUSLOG_INFO("Set vlog level, level:{},duration:{}", v.Get(), d.Get());

    return SetVlog(v.Get(), d.Get()).Then([&]() -> Response {
        return Ok("vlog set success!v=" + level + ", d=" + duration);
    });
}

Future<bool> HttpSysMgr::SetVlog(int level, const Duration &duration)
{
    // vlog level to output
    Set(level);
    if (level != orgLevel) {
        timeWatch = duration;
        (void)AsyncAfter(timeWatch.Remaining(), GetAID(), &HttpSysMgr::VlogReset);
    }
    return true;
}

}    // namespace http
}    // namespace litebus
