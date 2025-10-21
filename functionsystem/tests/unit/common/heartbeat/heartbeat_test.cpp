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
#include <gmock/gmock.h>

#include <async/future.hpp>

#include "heartbeat/heartbeat_observer.h"
#include "heartbeat/ping_pong_driver.h"
#include "heartbeat/heartbeat_observer_ctrl.h"
#include "ChildHeartbeatObserver.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
class Heartbeat : public ::testing::Test {};

TEST_F(Heartbeat, ObserverWithInvalidDst)
{
    HeartbeatObserveDriver heartbeatDriver("pinger", "invalid_dst", [](const litebus::AID &) {});
    auto ret = heartbeatDriver.Start();
    EXPECT_LE(ret, static_cast<int32_t>(StatusCode::CONN_ERROR));
}

TEST_F(Heartbeat, ObserverWithInvalidActorName)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    HeartbeatObserveDriver heartbeatDriver("pinger", "invalid_dst@127.0.0.1:" + std::to_string(port),
                                           [](const litebus::AID &) {});
    auto ret = heartbeatDriver.Start();
    EXPECT_LE(ret, 0);
}

TEST_F(Heartbeat, ObserverNormalExited)
{
    litebus::Promise<std::string> actorNamePromise;
    litebus::Promise<HeartbeatConnection> lostTypePromise;

    PingPongDriver pingpong("pinged", 1000,
        [actorNamePromise, lostTypePromise](const litebus::AID &aid, HeartbeatConnection type) {
            actorNamePromise.SetValue(aid.Name());
            lostTypePromise.SetValue(type);
        });
    litebus::AID observer;
    {
        HeartbeatObserveDriver heartbeatDriver("pinger", pingpong.GetActorAID(), 5, 10, [](const litebus::AID &) {});
        auto ret = heartbeatDriver.Start();
        EXPECT_EQ(ret, 0);
        observer = heartbeatDriver.GetActorAID();
    }
    auto name = actorNamePromise.GetFuture().Get(1000);
    EXPECT_EQ(name.IsSome(), true);
    EXPECT_EQ(name.Get(), observer.Name());

    auto type = lostTypePromise.GetFuture().Get(1000);
    EXPECT_EQ(type.IsSome(), true);
    EXPECT_EQ(type.Get(), HeartbeatConnection::EXITED);
}

TEST_F(Heartbeat, ObserverDetectedRemoteExited)
{
    litebus::Promise<std::string> actorNamePromise;

    PingPongDriver pingpong("pinged", 1000, [](const litebus::AID &aid, HeartbeatConnection type) {});
    HeartbeatObserveDriver heartbeatDriver("pinger", pingpong.GetActorAID(), 5, 10,
        [actorNamePromise](const litebus::AID &aid) { actorNamePromise.SetValue(aid.Name()); });
    auto ret = heartbeatDriver.Start();
    EXPECT_EQ(ret, 0);

    litebus::Terminate(pingpong.GetActorAID());
    litebus::Await(pingpong.GetActorAID());

    auto pingAID = pingpong.GetActorAID();
    auto name = actorNamePromise.GetFuture().Get(1000);
    EXPECT_EQ(name.IsSome(), true);
    EXPECT_EQ(name.Get(), pingAID.Name());
}

TEST_F(Heartbeat, ObserverDetectTimeOut)
{
    class NoResponsePingPong : public PingPongActor {
    public:
        NoResponsePingPong(std::string name)
            : PingPongActor(name, 1000, [](const litebus::AID &, HeartbeatConnection) {})
        {}
        ~NoResponsePingPong() override = default;
        void Ping(const litebus::AID &from, std::string &&name, std::string &&msg) override
        {
            count_++;
        }
        int count_ = 0;
    };

    auto noResponse = std::make_shared<NoResponsePingPong>("NoResponse");
    (void)litebus::Spawn(noResponse);

    int maxPingTimeoutNums = 5;
    litebus::Promise<std::string> timeoutActor;
    HeartbeatObserveDriver heartbeatDriver("pinger", noResponse->GetAID(), maxPingTimeoutNums, 10,
        [timeoutActor](const litebus::AID &actor) { timeoutActor.SetValue(actor.Name()); });
    auto ret = heartbeatDriver.Start();
    EXPECT_EQ(ret, 0);

    auto timeoutCallBackResult = timeoutActor.GetFuture().Get(100);
    EXPECT_EQ(timeoutCallBackResult.IsSome(), true);
    EXPECT_EQ(timeoutCallBackResult.Get(), noResponse->GetAID().Name());
    EXPECT_EQ(noResponse->count_, maxPingTimeoutNums);
    litebus::Terminate(noResponse->GetAID());
    litebus::Await(noResponse->GetAID());
}

TEST_F(Heartbeat, PingPongActorDetectTimeout)
{
    litebus::Promise<std::string> actorNamePromise;
    litebus::Promise<HeartbeatConnection> lostTypePromise;

    // pingIntervalMs > pingpongTimeMs can simulating pingpong get ping request timeout.
    uint32_t pingpongTimeMs = 100;
    uint32_t pingIntervalMs = 1000;
    PingPongDriver pingpong("pinged", pingpongTimeMs,
        [actorNamePromise, lostTypePromise](const litebus::AID &aid, HeartbeatConnection type) {
            actorNamePromise.SetValue(aid.Name());
            lostTypePromise.SetValue(type);
        });
    HeartbeatObserveDriver heartbeatDriver("pinger", pingpong.GetActorAID(), 5, pingIntervalMs,
        [](const litebus::AID &) {});
    auto ret = heartbeatDriver.Start();
    EXPECT_EQ(ret, 0);
    auto observer = heartbeatDriver.GetActorAID();

    auto name = actorNamePromise.GetFuture().Get(500);
    EXPECT_EQ(name.IsSome(), true);
    EXPECT_EQ(name.Get(), observer.Name());

    auto type = lostTypePromise.GetFuture().Get(500);
    EXPECT_EQ(type.IsSome(), true);
    EXPECT_EQ(type.Get(), HeartbeatConnection::LOST);
}

/**
 * Feature: HeartbeatObserver.
 * Description: To test HeartbeatObserver functions.
 * Steps:
 * 1. Init HeartbeatObserver.
 * Expectation: make parameters and invoke method to cover the
 * HeartbeatObserveDriver\HeartbeatObserver basic path codes
 */
TEST_F(Heartbeat, ObserverNormalStop)
{
    litebus::Promise<std::string> actorNamePromise;
    litebus::Promise<HeartbeatConnection> lostTypePromise;

    PingPongDriver pingpong("pinged", 1000,
        [actorNamePromise, lostTypePromise](const litebus::AID &aid, HeartbeatConnection type) {
            actorNamePromise.SetValue(aid.Name());
            lostTypePromise.SetValue(type);
        });

    HeartbeatObserveDriver heartbeatDriver("pinger", pingpong.GetActorAID(), 5, 10, [](const litebus::AID &) {});
    auto ret = heartbeatDriver.Start();
    EXPECT_EQ(ret, 0);
    // repeat start return 0
    EXPECT_EQ(heartbeatDriver.Start(), 0);
    auto ret1 = heartbeatDriver.Stop();
    EXPECT_EQ(ret1, 1);

    ChildHeartbeatObserver childHeartbeatObserver("pinger", pingpong.GetActorAID(), [](const litebus::AID &) {});
    childHeartbeatObserver.Exited(pingpong.GetActorAID());

    functionsystem::HeartbeatObserverCtrl heartbeatObserverCtrl(3, 100);
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    litebus::Future<Status> statusFuture = heartbeatObserverCtrl.Add(
        pingpong.GetActorAID(), "127.0.0.1:" + std::to_string(port), [](const litebus::AID &) {});
    EXPECT_EQ(statusFuture.Get().StatusCode(), StatusCode::SUCCESS);

    // repeated heartbeatObserverCtrl add action
    litebus::Future<Status> statusFutureRepeated = heartbeatObserverCtrl.Add(
        pingpong.GetActorAID(), "127.0.0.1:" + std::to_string(port), [](const litebus::AID &) {});
    EXPECT_EQ(statusFutureRepeated.Get().StatusCode(), StatusCode::SUCCESS);

    heartbeatObserverCtrl.Delete(pingpong.GetActorAID());
}
} // namespace functionsystem::test
