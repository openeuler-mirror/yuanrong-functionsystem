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

#include "common/profile/profiler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/profile/profile_timer.h"
#include "utils/os_utils.hpp"

namespace functionsystem::test {

class ProfileTest : public ::testing::Test {};

TEST_F(ProfileTest, ProfileTest)
{
    auto timer = std::make_shared<ProfileTimer>("timer");
    timer = nullptr;

    Profiler::Get().BeginSession("session1", "fake_file");
    Profiler::Get().EndSession();

    litebus::os::Rm("/tmp/profile");
    if (!litebus::os::ExistPath("/tmp/profile")) {
        litebus::os::Mkdir("/tmp/profile");
    }

    std::ofstream outfile;
    outfile.open("/tmp/profile/profile");
    outfile << "123";
    outfile.close();

    EXPECT_EQ(litebus::os::Read("/tmp/profile/profile").Get(), "123");
    Profiler::Get().BeginSession("session2", "/tmp/profile/profile");
    Profiler::Get().EndSession();

    timer = std::make_shared<ProfileTimer>("timer2");
    timer->StopTimer();

    EXPECT_THAT(litebus::os::Read("/tmp/profile/profile").Get(), testing::HasSubstr("otherData"));
    litebus::os::Rm("/tmp/profile");
}
}  // namespace functionsystem::test