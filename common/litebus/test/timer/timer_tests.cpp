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

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <math.h>

#include "actor/actorapp.hpp"
#include "async/asyncafter.hpp"
#include "litebus.hpp"
#include "timer/duration.hpp"
#include "timer/timertools.hpp"

using namespace std;

const std::string ACTORRECEIVE("ActorReceive");
std::string ACTORSEND("ActorSend");

namespace litebus {

Duration NowTime()
{
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    Duration duration = ts.tv_sec * 1000 + ts.tv_nsec / 1000 / 1000;
    return duration;
}

Duration timeBase = 100;
int timerNum = 30;
int timerStep = 50;

Duration maxCostTime = 0;
Duration minCostTime = 0;
Duration totalCostTime = 0;
constexpr Duration WATCH_INTERVAL = 20;

class TestActorReceive : public litebus::ActorBase {
public:
    TestActorReceive(std::string name) : ActorBase(name), duration(100), nums(0)
    {
    }

    ~TestActorReceive()
    {
    }

    void testTimerDuration(const litebus::Duration &startTime)
    {
        litebus::Duration nowTime = NowTime();
        duration = nowTime - startTime;
        BUSLOG_INFO("testTimerDuration {startTime, nowTime}= {{}, {}}", startTime, nowTime);
    }

    void testTimerNums(const litebus::Duration &startTime, const litebus::Duration &time)
    {
        Duration nowTime = NowTime();
        Duration costTime = nowTime - startTime - time;
        Duration avgCostTime = 0;
        double varCostTime = 0;
        if (nums == 0) {
            minCostTime = costTime;
        }
        nums++;
        totalCostTime += costTime;
        costTimePool.push_back(costTime);
        BUSLOG_DEBUG("{costTime, totalCostTime, nums}= {{}, {}, {}}", costTime, totalCostTime, nums);
        if (nums == timerNum) {
            avgCostTime = totalCostTime / nums;
            for (unsigned int i = 0; i < costTimePool.size(); i++) {
                maxCostTime = costTimePool[i] > maxCostTime ? costTimePool[i] : maxCostTime;
                minCostTime = costTimePool[i] < minCostTime ? costTimePool[i] : minCostTime;
                double diff = static_cast<double>(costTimePool[i]) - avgCostTime;
                varCostTime += pow(diff, 2);
                BUSLOG_DEBUG("{varCost, totalCost, i, costTime, avgCost, pow, size}= {{}, {}, {}, {}, {}, {}, {}}",
                             costTime, totalCostTime, i, costTimePool[i], avgCostTime,
                             pow((costTimePool[i] - avgCostTime), 2), costTimePool.size());
            }
            varCostTime /= (costTimePool.size() * 1.0);
            BUSLOG_INFO("testTimerNums {startTime, max, min, avg, var, nums}= {{}, {}, {}, {}, {}, {}}", startTime,
                        maxCostTime, minCostTime, avgCostTime, varCostTime, nums);
        }
    }

    Duration GetDuration()
    {
        return duration;
    }

    int GetNums()
    {
        return nums;
    }

protected:
    virtual void Init() override
    {
    }

private:
    Duration duration;
    int nums;
    std::vector<Duration> costTimePool;
};

class TestActorSend : public litebus::ActorBase {
public:
    TestActorSend(std::string name) : ActorBase(name)
    {
    }

    ~TestActorSend()
    {
    }

    void StartAddNewTimer(litebus::Duration &time)
    {
        const Duration startTime = NowTime();
        AsyncAfter(time, ACTORRECEIVE, &TestActorReceive::testTimerDuration, startTime);
    }

    void StartCancelTimer()
    {
        const Duration time = NowTime();
        const Timer &timer = AsyncAfter(timeBase, ACTORRECEIVE, &TestActorReceive::testTimerDuration, time);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeBase / 2));
        TimerTools::Cancel(timer);
    }

    void StartAddImmediateTimer()
    {
        const Duration time = NowTime();
        BUSLOG_INFO("{now}= {}", time);
        AsyncAfter(0, ACTORRECEIVE, &TestActorReceive::testTimerDuration, time);
    }

    void StartNumsTimer(const litebus::Duration &time)
    {
        const Duration startTime = NowTime();
        AsyncAfter(time, ACTORRECEIVE, &TestActorReceive::testTimerNums, startTime, time);
    }

private:
    virtual void Init() override
    {
    }
};

class TimerTest : public ::testing::Test {
protected:
    void SetUp()
    {
        BUSLOG_INFO("{timer gtest start}");
    }
    void TearDown()
    {
        BUSLOG_INFO("timer gtest stop");
        litebus::TerminateAll();
    }
};

TEST_F(TimerTest, AddNewTimer)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    auto p_actorsend = std::make_shared<TestActorSend>(ACTORSEND);

    litebus::Spawn(p_actorreceive);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    litebus::Spawn(p_actorsend);
    BUSLOG_INFO("after spawn");
    p_actorsend->StartAddNewTimer(timeBase);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(true, (p_actorreceive->GetDuration() - timeBase) < 20);
}

TEST_F(TimerTest, AddImmediateTimer)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    auto p_actorsend = std::make_shared<TestActorSend>(ACTORSEND);

    litebus::Spawn(p_actorreceive);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    litebus::Spawn(p_actorsend);
    BUSLOG_INFO("after spawn");
    p_actorsend->StartAddImmediateTimer();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(true, p_actorreceive->GetDuration() < 10);
}

TEST_F(TimerTest, MultiTimerOneThreadSmaller)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    auto p_actorsend = std::make_shared<TestActorSend>(ACTORSEND);

    litebus::Spawn(p_actorreceive);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    litebus::Spawn(p_actorsend);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BUSLOG_INFO("after spawn");
    for (int i = 0; i < timerNum; i++) {
        Duration time = timeBase + (timerNum - i) * timerStep;
        p_actorsend->StartNumsTimer(time);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10 * timeBase + timerNum * timerStep));

    EXPECT_EQ(timerNum, p_actorreceive->GetNums());
}

TEST_F(TimerTest, MultiTimerOneThreadBigger)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    auto p_actorsend = std::make_shared<TestActorSend>(ACTORSEND);

    litebus::Spawn(p_actorreceive);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    litebus::Spawn(p_actorsend);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BUSLOG_INFO("after spawn");
    for (int i = 0; i < timerNum; i++) {
        Duration time = timeBase + i * timerStep;
        p_actorsend->StartNumsTimer(time);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10 * timeBase + timerNum * timerStep));

    EXPECT_EQ(timerNum, p_actorreceive->GetNums());
}

TEST_F(TimerTest, WatchTimer)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    auto p_actorsend = std::make_shared<TestActorSend>(ACTORSEND);

    litebus::Spawn(p_actorreceive);

    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    litebus::Spawn(p_actorsend);

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BUSLOG_INFO("after spawn");
    p_actorsend->StartAddImmediateTimer();
    for (int i = 0; i < 10; i++) {
        Duration time = 100 * timeBase;
        p_actorsend->StartNumsTimer(time);
    }

    for (int i = 10; i < timerNum; i++) {
        Duration time = (WATCH_INTERVAL + 2) * 1000;
        p_actorsend->StartNumsTimer(time);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds((WATCH_INTERVAL + 5) * 1000));

    EXPECT_EQ(timerNum, p_actorreceive->GetNums());
}

TEST_F(TimerTest, MultiTimerMultiThread)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    std::vector<shared_ptr<TestActorSend>> sendPoll;
    for (int i = 0; i < timerNum; i++) {
        sendPoll.push_back(std::make_shared<TestActorSend>(ACTORSEND + to_string(i)));
    }

    litebus::Spawn(p_actorreceive);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    for (int i = 0; i < timerNum; i++) {
        litebus::Spawn(sendPoll[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Duration time = timeBase + i * timerStep;
        sendPoll[i]->StartNumsTimer(time);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(timeBase + timerNum * timerStep));

    EXPECT_EQ(timerNum, p_actorreceive->GetNums());
}

TEST_F(TimerTest, TimeWatchFunction)
{
    Duration duration = 1000;
    TimeWatch timeWatch1 = TimeWatch::In(1000);
    TimeWatch timeWatch2 = 1000;
    timeWatch2 = duration;
    TimeWatch timeWatch3 = TimeWatch::In(2000);
    BUSLOG_INFO("{timeWatch1, timeWatch2, timeWatch3}= {{}, {}, {}}", timeWatch1.Time(), timeWatch2.Time(),
                timeWatch3.Time());
    EXPECT_EQ(true, timeWatch1 <= timeWatch2);
    EXPECT_EQ(true, timeWatch1 < timeWatch3);
    EXPECT_FALSE(timeWatch2 == timeWatch3);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_EQ(true, timeWatch1.Expired());
    EXPECT_EQ(true, timeWatch3.Remaining() > 0);
    BUSLOG_INFO("timeWatch3 remaining={}", timeWatch3.Remaining());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    BUSLOG_INFO("timeWatch3 remaining={}", timeWatch3.Remaining());
    EXPECT_EQ(true, timeWatch3.Remaining() == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    BUSLOG_INFO("timeWatch3 remaining={}", timeWatch3.Remaining());
    EXPECT_EQ(true, timeWatch3.Remaining() == 0);
}

TEST_F(TimerTest, CancelTimer)
{
    auto p_actorreceive = std::make_shared<TestActorReceive>(ACTORRECEIVE);
    auto p_actorsend = std::make_shared<TestActorSend>(ACTORSEND);

    litebus::Spawn(p_actorreceive);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    litebus::Spawn(p_actorsend);
    BUSLOG_INFO("after spawn");
    p_actorsend->StartCancelTimer();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(true, p_actorreceive->GetDuration() == 100);
}
}    // namespace litebus
