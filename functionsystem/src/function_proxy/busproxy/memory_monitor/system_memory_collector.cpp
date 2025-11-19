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

#include "system_memory_collector.h"

#include <actor/actor.hpp>
#include <async/async.hpp>
#include <async/asyncafter.hpp>
#include <fstream>

#include "common/logs/logging.h"

namespace functionsystem {

const uint32_t SYS_MEMORY_USAGE_DETECT_INTERVAL = 50;
const std::string MEMORY_USAGE_PATH = "/sys/fs/cgroup/memory/memory.stat";
const std::string MEMORY_LIMIT_PATH = "/sys/fs/cgroup/memory/memory.limit_in_bytes";

SystemMemoryCollector::SystemMemoryCollector(const std::string &name) : ActorBase(name), rssPath_(MEMORY_USAGE_PATH)
{
}

void SystemMemoryCollector::Init()
{
    YRLOG_INFO("init SystemMemoryCollector {}", ActorBase::GetAID().Name());
    procFSTools_ = std::make_shared<ProcFSTools>();
}

void SystemMemoryCollector::Finalize()
{
    YRLOG_INFO("finalize SystemMemoryCollector {}", ActorBase::GetAID().Name());
    (void)litebus::TimerTools::Cancel(nextTimer_);
}

uint64_t SystemMemoryCollector::GetLimit() const
{
    return limitUsage_.load();
}

uint64_t SystemMemoryCollector::GetCurrent() const
{
    return currentUsage_.load();
}

void SystemMemoryCollector::SetLimit()
{
    limitUsage_.store(GetMemoryUsage(MEMORY_LIMIT_PATH));
}
void SystemMemoryCollector::SetCurrent()
{
    currentUsage_.store(GetRssUsage(rssPath_));
}

uint64_t SystemMemoryCollector::GetRssUsage(const std::string &path) const
{
    auto realPath = litebus::os::RealPath(path);
    if (realPath.IsNone()) {
        YRLOG_ERROR("failed to get realpath: {}", path);
        return 0;
    }

    std::string line;
    std::ifstream statFile(realPath.Get());

    if (!statFile.is_open()) {
        YRLOG_ERROR("unable to open {}", path);
        return 0;
    }
    while (getline(statFile, line)) {
        if (line.find("rss") != std::string::npos) {
            try {
                auto rss = std::stol(line.substr(line.find_last_of(" ") + 1));
                statFile.close();
                return static_cast<uint64_t>(rss);
            } catch (const std::invalid_argument &e) {
                YRLOG_ERROR("failed to transform proc memory data, caused by invalid_argument error: {}", e.what());
            } catch (const std::out_of_range &e) {
                YRLOG_ERROR("failed to transform proc memory data, caused by out_of_range error: {}", e.what());
            }
            break;
        }
    }
    statFile.close();
    return 0;
}

uint64_t SystemMemoryCollector::GetMemoryUsage(const std::string &path)
{
    ASSERT_IF_NULL(procFSTools_);
    auto content = procFSTools_->Read(path);
    if (content.IsNone() || content.Get().empty()) {
        YRLOG_ERROR("read content from {} failed.", path);
        return 0;
    }
    auto status = content.Get();
    try {
        auto data = std::stod(litebus::strings::Trim(status));
        return static_cast<uint64_t>(data);
    } catch (const std::invalid_argument &e) {
        YRLOG_ERROR("failed to transform proc memory data, caused by invalid_argument error: {}", e.what());
    } catch (const std::out_of_range &e) {
        YRLOG_ERROR("failed to transform proc memory data, caused by out_of_range error: {}", e.what());
    }
    return 0;
}

void SystemMemoryCollector::RefreshActualMemoryUsage()
{
    SetLimit();
    SetCurrent();
    nextTimer_ = litebus::AsyncAfter(SYS_MEMORY_USAGE_DETECT_INTERVAL, GetAID(),
                                     &SystemMemoryCollector::RefreshActualMemoryUsage);
}

void SystemMemoryCollector::StopRefreshActualMemoryUsage()
{
    YRLOG_INFO("SystemMemoryCollector stop updating memory usage.");
    (void)litebus::TimerTools::Cancel(nextTimer_);
}
}  // namespace functionsystem