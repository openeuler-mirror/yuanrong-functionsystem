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

#ifndef FUNCTIONSYSTEM_PROFILER_H
#define FUNCTIONSYSTEM_PROFILER_H

#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace functionsystem {

#ifdef PROFILING
#define PROFILE_BEGIN_SESSION(name, filepath) Profiler::Get().BeginSession(name, filepath)
#define PROFILE_END_SESSION() Profiler::Get().EndSession()
#define PROFILE_SCOPE(name) ProfileTimer timer##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__func__)
#else
#define PROFILE_SCOPE(name)
#endif

struct ProfileResult {
    std::string name;
    std::chrono::duration<double, std::micro> start;
    std::chrono::microseconds elapsedTime;
    std::thread::id threadId;
};

struct ProfileSession {
    explicit ProfileSession(const std::string &name) : name(name)
    {
    }
    std::string name;
};

class Profiler {
public:
    static Profiler &Get();
    Profiler(const Profiler &) = delete;
    Profiler(Profiler &&) = delete;
    Profiler &operator=(const Profiler &) = delete;
    Profiler &operator=(Profiler &&) = delete;

    void BeginSession(const std::string &name, const std::string &filepath = "profile.json");

    void EndSession() noexcept;

    void WriteProfile(const ProfileResult &result);

private:
    Profiler();
    ~Profiler();
    void WriteHeader();
    void WriteFooter();
    void InternalEndSession();

    std::mutex mutex_;
    std::unique_ptr<ProfileSession> currentSession_;
    std::ofstream outputStream_;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_PROFILER_H
