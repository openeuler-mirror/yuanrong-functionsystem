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
#include "external_system_collector.h"

#include "curl/curl.h"
#include "nlohmann/json.hpp"

#include "utils/os_utils.hpp"
namespace functionsystem::runtime_manager {
const uint32_t CPU_SCALE = 1000;
const uint32_t MEMORY_SCALE = 1024 * 1024;
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp)
{
    size_t totalSize = size * nmemb;
    userp->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

CurlHelper::CurlHelper(const std::string &name, const std::string &externEndpoint, const std::string &url)
    : litebus::ActorBase(name), externEndpoint_(externEndpoint), url_(url)
{
    auto error = curl_global_init(CURL_GLOBAL_ALL);
    if (error) {
        std::cerr << "<CurlHelper> failed to initialize global curl!" << std::endl;
        return;
    }
    curl_ = curl_easy_init();
    if (!curl_) {
        curl_global_cleanup();
        std::cerr << "<CurlHelper> failed to initialize easy curl!" << std::endl;
        return;
    }
};

CurlHelper::~CurlHelper()
{
    curl_easy_cleanup(curl_);
    curl_global_cleanup();
    if (curl_ != nullptr) {
        curl_ = nullptr;
    }
}

litebus::Future<std::string> CurlHelper::Query()
{
    if (!curl_) {
        YRLOG_ERROR("Failed to initialize libcurl");
        return "";
    }
    curl_easy_reset(curl_);
    std::string response;
    curl_easy_setopt(curl_, CURLOPT_UNIX_SOCKET_PATH, externEndpoint_.c_str());
    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        YRLOG_DEBUG_COUNT_60("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        return "";
    }
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        YRLOG_DEBUG_COUNT_60("{} for {} with status code: {}, response: {}", externEndpoint_, url_, http_code,
                             response);
        return "";
    }
    return response;
}

litebus::Future<std::string> ExternalSystemCollector::CollectFromExternal() const
{
    return litebus::Async(curlActorRef_->GetAID(), &CurlHelper::Query);
}

litebus::Future<Metric> ExternalSystemCPUCollector::GetUsage() const
{
    return CollectFromExternal().Then([previous(previous_)](const std::string& response) -> litebus::Future<Metric> {
        YRLOG_DEBUG_COUNT_60("Received CPU response: {}", response);
        if (response.empty()) {
            return previous != nullptr ? *previous : Metric{};
        }
        // Parse JSON response and return Metric
        try {
            auto j = nlohmann::json::parse(response);
            if (j.contains("cpu") && j["cpu"].is_number_integer()) {
                auto cpu = j["cpu"].get<int>();
                auto metric = Metric{ { double(cpu * CPU_SCALE) }, {}, {}, {} } ;
                *previous = metric;
                return metric;
            }
        } catch (const std::exception& e) {
            YRLOG_DEBUG_COUNT_60("Failed to parse JSON response: {}, error: {}", response, e.what());
        }
        return previous != nullptr ? *previous : Metric{};
    });
}

Metric ExternalSystemCPUCollector::GetLimit() const
{
    if (previous_ == nullptr) {
        YRLOG_DEBUG_COUNT_60("Failed to Get CPU Limit, fallback to default");
        return this->GetLimit();
    }
    return *previous_;
}

litebus::Future<Metric> ExternalSystemMemoryCollector::GetUsage() const
{
    return CollectFromExternal().Then([previous(previous_)](const std::string& response) -> litebus::Future<Metric> {
        YRLOG_DEBUG_COUNT_60("Received Memory response: {}", response);
        if (response.empty()) {
            return previous != nullptr ? *previous : Metric{};
        }
        // Parse JSON response and return Metric
        try {
            auto j = nlohmann::json::parse(response);
            if (j.contains("mem") && j["mem"].is_number_integer()) {
                auto memory = j["mem"].get<int64_t>();
                auto metric = Metric{ { double(memory) / MEMORY_SCALE }, {}, {}, {} };
                *previous = metric;
                return metric;
            }
        } catch (const std::exception& e) {
            YRLOG_DEBUG_COUNT_60("Failed to parse JSON response: {}, error: {}", response, e.what());
        }
        return previous != nullptr ? *previous : Metric{};
    });
}
Metric ExternalSystemMemoryCollector::GetLimit() const
{
    if (previous_ == nullptr) {
        YRLOG_DEBUG_COUNT_60("Failed to Get Memory Limit, fallback to default");
        return this->GetLimit();
    }
    return *previous_;
}
}  // namespace functionsystem::runtime_manager