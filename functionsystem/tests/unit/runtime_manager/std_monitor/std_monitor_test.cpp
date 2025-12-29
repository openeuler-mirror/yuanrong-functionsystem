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

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "gtest/gtest.h"
#include "runtime_manager/std_monitor/std_monitor.h"
#include "runtime_manager/utils/utils.h"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {
const int DEFAULT_STD_MONITOR_EVENT = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR;

class StdMonitorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        stdMonitor_ = std::make_shared<StdMonitor>();
        stdMonitor_->Start();
    }

    void TearDown() override
    {
        if (stdMonitor_) {
            stdMonitor_->Stop();
            stdMonitor_ = nullptr;
        }
    }
protected:
    std::shared_ptr<StdMonitor> stdMonitor_{ nullptr };
};

TEST_F(StdMonitorTest, AddValidFd)
{
    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    EXPECT_TRUE(stdMonitor_->AddFd(pipeFds[0], DEFAULT_STD_MONITOR_EVENT, nullptr).Get());
    close(pipeFds[0]);
    close(pipeFds[1]);
}

TEST_F(StdMonitorTest, AddInvalidFd)
{
    // 1.fd = -1
    EXPECT_FALSE(stdMonitor_->AddFd(-1, DEFAULT_STD_MONITOR_EVENT, nullptr).Get());

    // 2.fd = 9999 -- non-existing fd
    EXPECT_FALSE(stdMonitor_->AddFd(9999, DEFAULT_STD_MONITOR_EVENT, nullptr).Get());
}

TEST_F(StdMonitorTest, RemoveFd)
{
    // 1.remove a real fd
    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    EXPECT_TRUE(stdMonitor_->AddFd(pipeFds[0], DEFAULT_STD_MONITOR_EVENT, nullptr).Get());
    EXPECT_TRUE(stdMonitor_->RemoveFd(pipeFds[0]).Get());
    close(pipeFds[0]);
    close(pipeFds[1]);

    // 2.remove a invalid fd
    EXPECT_FALSE(stdMonitor_->RemoveFd(-1).Get());

    // 3.remove a non-existing fd (may have been already removed)
    EXPECT_TRUE(stdMonitor_->RemoveFd(9999).Get());
}

}  // namespace functionsystem::test
