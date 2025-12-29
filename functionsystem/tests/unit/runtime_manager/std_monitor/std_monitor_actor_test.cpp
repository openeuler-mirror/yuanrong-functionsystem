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
#include "async/async.hpp"
#include "runtime_manager/std_monitor/std_monitor_actor.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
const int DEFAULT_STD_MONITOR_EVENT = EPOLLIN | EPOLLHUP | EPOLLERR;

class StdMonitorActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        std::string name = "StdMonitor_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        actor_ = std::make_shared<StdMonitorActor>(name);
        litebus::Spawn(actor_);
        auto result = actor_->Start();
        EXPECT_TRUE(result.Get());
    }

    void TearDown() override
    {
        if (actor_) {
            actor_->Stop();
            litebus::Terminate(actor_->GetAID());
            litebus::Await(actor_->GetAID());
        }
        actor_ = nullptr;
    }
protected:
    std::shared_ptr<StdMonitorActor> actor_{ nullptr };
};

// * Test case for adding valid FD
// * 1. Create a valid pipe
// * 2. Add read fd to monitor
// * 3. Verify callback registration
TEST_F(StdMonitorActorTest, AddValidFdWithCallback) {
    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);

    bool callbackTriggered = false;
    auto callback = [&callbackTriggered](int fd, int event) {
        callbackTriggered = true;
    };

    auto addResult = litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, pipeFds[0],
                                    DEFAULT_STD_MONITOR_EVENT, callback).Get();
    EXPECT_TRUE(addResult);

    // Trigger event by writing to pipe
    char data[] = "test";
    write(pipeFds[1], data, sizeof(data));

    // Wait for event processing
    ASSERT_AWAIT_TRUE([&callbackTriggered]() -> bool { return callbackTriggered == true; });

    close(pipeFds[0]);
    close(pipeFds[1]);
}

// * Test case for invalid fd handling
// * 1. Test negative fd
// * 2. Test fd that doesn't exist
TEST_F(StdMonitorActorTest, AddInvalidFdVariants) {
    // Test negative FD
    auto result1 = litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, -1,
                                  DEFAULT_STD_MONITOR_EVENT, nullptr).Get();
    EXPECT_FALSE(result1);

    // Test non-existing FD
    auto result3 = litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, 9999,
                                  DEFAULT_STD_MONITOR_EVENT, nullptr).Get();
    EXPECT_FALSE(result3);
}

// * Test case for multiple fd monitoring
// * 1. Add three different fds
// * 2. Trigger each fd separately
// * 3. Verify callbacks are called in order
TEST_F(StdMonitorActorTest, MultipleFdMonitoring) {
    int fd1[2], fd2[2], fd3[2];
    pipe(fd1);
    pipe(fd2);
    pipe(fd3);

    std::vector<bool> callbacksTriggered(3, false);
    auto cb1 = [&callbacksTriggered](int, int) { callbacksTriggered[0] = true; };
    auto cb2 = [&callbacksTriggered](int, int) { callbacksTriggered[1] = true; };
    auto cb3 = [&callbacksTriggered](int, int) { callbacksTriggered[2] = true; };

    litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, fd1[0], DEFAULT_STD_MONITOR_EVENT, cb1).Get();
    litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, fd2[0], DEFAULT_STD_MONITOR_EVENT, cb2).Get();
    litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, fd3[0], DEFAULT_STD_MONITOR_EVENT, cb3).Get();

    // Trigger each FD
    write(fd1[1], "1", 1);
    write(fd2[1], "2", 1);
    write(fd3[1], "3", 1);

    for (auto&& value : callbacksTriggered) {
        ASSERT_AWAIT_TRUE([&value]() -> bool { return value == true; });
    }

    close(fd1[0]); close(fd1[1]);
    close(fd2[0]); close(fd2[1]);
    close(fd3[0]); close(fd3[1]);
}

// * Test case for fd removal
// * 1. Add fd and then remove it --> success
// * 2. Remove invalid fd --> fail
// * 2. Remove a non-existing fd --> success(may have been already removed)
TEST_F(StdMonitorActorTest, FdRemovalValidation) {
    // 1.remove a real fd
    int pipeFds[2];
    pipe(pipeFds);

    auto addResult = litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, pipeFds[0],
                                    DEFAULT_STD_MONITOR_EVENT, nullptr).Get();
    EXPECT_TRUE(addResult);

    auto removeResult = litebus::Async(actor_->GetAID(), &StdMonitorActor::RemoveFd, pipeFds[0]).Get();
    EXPECT_TRUE(removeResult);
    close(pipeFds[0]);
    close(pipeFds[1]);

    // 2.remove a invalid fd
    EXPECT_FALSE(actor_->RemoveFd(-1).Get());

    // 3.remove a non-existing
    EXPECT_TRUE(actor_->RemoveFd(9999).Get());
}

// * Test case for eventfd stop mechanism
// * 1. Trigger stop event
// * 2. Verify run loop exit by adding a valid fd --> fail
TEST_F(StdMonitorActorTest, StopEventfdMechanism) {
    actor_->Stop();

    // add a valid fd
    int pipeFds[2];
    pipe(pipeFds);

    auto addResult = litebus::Async(actor_->GetAID(), &StdMonitorActor::AddFd, pipeFds[0],
                                    DEFAULT_STD_MONITOR_EVENT, nullptr).Get();
    EXPECT_FALSE(addResult);
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}

}  // namespace functionsystem::test
