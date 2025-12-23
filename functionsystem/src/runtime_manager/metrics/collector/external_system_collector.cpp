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
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp)
{
    size_t totalSize = size * nmemb;
    userp->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
litebus::Future<std::string> CurlHelper::Query()
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        YRLOG_ERROR("Failed to initialize libcurl");
        return "";
    }
    std::string response;
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, externEndpoint_);
    curl_easy_setopt(curl, CURLOPT_URL, url_);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        YRLOG_DEBUG_COUNT_60("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        YRLOG_DEBUG_COUNT_60("{} for {} with status code: {}, response: {}", externEndpoint_, url_, http_code,
                             response);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

litebus::Future<std::string> ExternalSystemCollector::CollectFromExternal() const
{
    return litebus::Async(curlActorRef_->GetAID(), &CurlHelper::Query);
}

litebus::Future<Metric> ExternalSystemCPUCollector::GetUsage() const
{
    return CollectFromExternal().Then([previous(previous_)](const std::string& response) -> litebus::Future<Metric> {
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
        if (response.empty()) {
            return previous != nullptr ? *previous : Metric{};
        }
        // Parse JSON response and return Metric
        try {
            auto j = nlohmann::json::parse(response);
            if (j.contains("mem") && j["mem"].is_number_integer()) {
                auto memory = j["mem"].get<int>();
                auto metric = Metric{ { double(memory) }, {}, {}, {} };
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