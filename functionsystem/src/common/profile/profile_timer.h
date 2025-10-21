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

#ifndef FUNCTIONSYSTEM_PROFILE_TIMER_H
#define FUNCTIONSYSTEM_PROFILE_TIMER_H

#include <chrono>
#include <string>

namespace functionsystem {

class ProfileTimer {
public:
    explicit ProfileTimer(const std::string &name);
    ~ProfileTimer() noexcept;

    void StopTimer();

private:
    std::string name_;
    std::chrono::time_point<std::chrono::steady_clock> startTimePoint_;
    bool stopped_ = false;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_PROFILE_TIMER_H
