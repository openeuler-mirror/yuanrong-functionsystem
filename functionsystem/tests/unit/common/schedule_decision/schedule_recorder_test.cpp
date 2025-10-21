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
#include "common/schedule_decision/schedule_recorder/schedule_recorder.h"

#include <gtest/gtest.h>

#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace schedule_decision;
class ScheduleRecorderTest : public ::testing::Test {};

TEST_F(ScheduleRecorderTest, EmptyQuery)
{
    auto recorder = ScheduleRecorder::CreateScheduleRecorder();
    auto future = recorder->TryQueryScheduleErr("123");
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), true);
    recorder = nullptr;
}

TEST_F(ScheduleRecorderTest, RecordScheduleErr)
{
    auto recorder = ScheduleRecorder::CreateScheduleRecorder();
    recorder->RecordScheduleErr("123", Status(StatusCode::ERR_RESOURCE_NOT_ENOUGH, "no available cpu"));
    auto future = recorder->TryQueryScheduleErr("123");
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), false);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::ERR_RESOURCE_NOT_ENOUGH);
    future = recorder->TryQueryScheduleErr("123");
    EXPECT_EQ(future.Get().IsOk(), true);
    recorder = nullptr;
}

TEST_F(ScheduleRecorderTest, MultipleRecordScheduleErr)
{
    auto recorder = ScheduleRecorder::CreateScheduleRecorder();
    recorder->RecordScheduleErr("123", Status(StatusCode::ERR_RESOURCE_NOT_ENOUGH, "no available cpu"));
    recorder->RecordScheduleErr("123", Status(StatusCode::ERR_GROUP_SCHEDULE_FAILED, "no available mem"));
    auto future = recorder->TryQueryScheduleErr("123");
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().IsOk(), false);
    EXPECT_EQ(future.Get().StatusCode(), StatusCode::ERR_GROUP_SCHEDULE_FAILED);
    recorder->EraseScheduleErr("123");
    future = recorder->TryQueryScheduleErr("123");
    EXPECT_EQ(future.Get().IsOk(), true);
    recorder = nullptr;
}
}  // namespace functionsystem::test