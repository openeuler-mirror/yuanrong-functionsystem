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

#ifndef HTTP_VLOGMGR_HPP
#define HTTP_VLOGMGR_HPP

#include "actor/buslog.hpp"
#include "httpd/http_actor.hpp"

namespace litebus {
namespace http {
class HttpSysMgr : public HttpActor {
public:
    explicit HttpSysMgr(const std::string &name) : HttpActor(name), orgLevel(0)
    {
    }

    ~HttpSysMgr() override
    {
    }

    Future<bool> SetVlog(int level, const Duration &duration);

protected:
    void Init() override
    {
        BUSLOG_INFO("Initialize Vlog Manager");
        AddRoute("/toggle", &HttpSysMgr::VlogToggle);
    }

private:
    Future<http::Response> VlogToggle(const http::Request &request);

    void Set(int) const
    {
        BUSLOG_ERROR("unsupported set FLAGS_v");
    }

    void VlogReset()
    {
        if (timeWatch.Expired()) {
            Set(orgLevel);
        }
    }

    TimeWatch timeWatch;
    const int32_t orgLevel;
};

}    // namespace http
}    // namespace litebus
#endif
