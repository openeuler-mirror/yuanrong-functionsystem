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

#include <gtest/gtest.h>

#include "sdk/include/observe_actor.h"

namespace observability::test {

class ObserveActorTest : public testing::Test {
protected:
    void SetUp()
    {
        observeActor_ = std::make_shared<sdk::metrics::ObserveActor>();
        litebus::Spawn(observeActor_);
        std::function<void(int)> cb = [=](int interval) { (void)interval; value_++; };
        observeActor_->RegisterCollectFunc(cb);
    }

    void TearDown()
    {
        if (observeActor_ != nullptr) {
            litebus::Terminate(observeActor_->GetAID());
            litebus::Await(observeActor_);
            observeActor_ = nullptr;
        }
        value_ = 0;
    }

    inline static std::shared_ptr<sdk::metrics::ObserveActor> observeActor_;
    inline static int value_ = 0;
};

TEST_F(ObserveActorTest, RegisterInvalidTimer)
{
    int interval = 0;
    observeActor_->RegisterTimer(interval);
    ASSERT_EQ(observeActor_->GetCollectIntervals().size(), static_cast<uint64_t>(0));
    auto collectTimerMap = observeActor_->GetCollectTimerMap();
    std::cout << "collectTimerMap" << collectTimerMap.size();
    ASSERT_TRUE(collectTimerMap.find(interval) == collectTimerMap.end());
}

TEST_F(ObserveActorTest, RegisterValidTimer)
{
    int interval = 1;
    observeActor_->RegisterTimer(interval);
    ASSERT_EQ(observeActor_->GetCollectIntervals().size(), static_cast<uint64_t>(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_TRUE(observeActor_->GetCollectTimerMap().find(interval) != observeActor_->GetCollectTimerMap().end());
}

TEST_F(ObserveActorTest, CronTimer)
{
    int interval = 1;
    value_ = 0;
    observeActor_->RegisterTimer(interval);
    ASSERT_EQ(observeActor_->GetCollectIntervals().size(), static_cast<uint64_t>(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    ASSERT_TRUE(value_ >= 2);
}
}
