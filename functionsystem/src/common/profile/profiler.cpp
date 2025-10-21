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

#include "profiler.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>

namespace functionsystem {

static const int DISPLAY_PRECISION = 3;

Profiler &Profiler::Get()
{
    static Profiler instance{};
    return instance;
}

Profiler::Profiler() : currentSession_(nullptr)
{
}

Profiler::~Profiler()
{
    EndSession();
}

void Profiler::Profiler::BeginSession(const std::string &name, const std::string &filepath)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (currentSession_ != nullptr) {
        InternalEndSession();
    }
    outputStream_.open(filepath);
    if (outputStream_.is_open()) {
        currentSession_ = std::make_unique<ProfileSession>(name);
        WriteHeader();
    } else {
        // Failed to open output stream.
    }
}

void Profiler::EndSession() noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    InternalEndSession();
}

void Profiler::WriteProfile(const ProfileResult &result)
{
    std::stringstream ss;
    ss << std::setprecision(DISPLAY_PRECISION) << std::fixed;
    ss << ",{";
    ss << R"("cat":"function",)";
    ss << R"("dur":)" << (result.elapsedTime.count()) << ",";
    ss << R"("name":")" << result.name << R"(",)";
    ss << R"("ph":"X",)";
    ss << R"("pid":0,)";
    ss << R"("tid":)" << result.threadId << ",";
    ss << R"("ts":)" << result.start.count();
    ss << "}";

    std::lock_guard<std::mutex> guard(mutex_);

    if (currentSession_ != nullptr) {
        outputStream_ << ss.str();
        (void)outputStream_.flush();
    }
}

void Profiler::WriteHeader()
{
    outputStream_ << R"({"otherData": {},"traceEvents":[{})";
    (void)outputStream_.flush();
}

void Profiler::WriteFooter()
{
    outputStream_ << "]}";
    (void)outputStream_.flush();
}

void Profiler::InternalEndSession()
{
    if (currentSession_ != nullptr) {
        WriteFooter();
        outputStream_.close();
        currentSession_ = nullptr;
    }
}

}  // namespace functionsystem