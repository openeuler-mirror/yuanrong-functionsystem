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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "function_proxy/local_scheduler/local_sched_driver.h"
#include "function_proxy/local_scheduler/tcp_tunnel_server.h"
#include "mocks/mock_resource_view_mgr.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::local_scheduler;

TEST(LocalSchedulingApiRouterTest, PostPauseSchedulingMarksUnitsEvicting)
{
    auto resourceViewMgr = std::make_shared<MockResourceViewMgr>();
    LocalSchedulingApiRouter router;
    router.InitUpdateSchedulingStatusHandler(resourceViewMgr);

    auto handlers = router.GetHandlers();
    ASSERT_NE(handlers, nullptr);
    ASSERT_NE(handlers->find("/localschedulingstatus"), handlers->end());

    HttpRequest request;
    request.method = "POST";

    EXPECT_CALL(*resourceViewMgr, UpdateAllUnitStatus(resource_view::UnitStatus::EVICTING))
        .WillOnce(Return(AsyncReturn(Status::OK())));

    auto response = handlers->at("/localschedulingstatus")(request);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
    EXPECT_THAT(response.Get().body, HasSubstr("evicting"));
}

TEST(LocalSchedulingApiRouterTest, DeletePauseSchedulingRestoresNormalStatus)
{
    auto resourceViewMgr = std::make_shared<MockResourceViewMgr>();
    LocalSchedulingApiRouter router;
    router.InitUpdateSchedulingStatusHandler(resourceViewMgr);

    auto handlers = router.GetHandlers();
    ASSERT_NE(handlers, nullptr);
    ASSERT_NE(handlers->find("/localschedulingstatus"), handlers->end());

    HttpRequest request;
    request.method = "DELETE";

    EXPECT_CALL(*resourceViewMgr, UpdateAllUnitStatus(resource_view::UnitStatus::NORMAL))
        .WillOnce(Return(AsyncReturn(Status::OK())));

    auto response = handlers->at("/localschedulingstatus")(request);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::OK);
    EXPECT_THAT(response.Get().body, HasSubstr("normal"));
}

TEST(LocalSchedulingApiRouterTest, UnsupportedMethodReturnsMethodNotAllowed)
{
    auto resourceViewMgr = std::make_shared<MockResourceViewMgr>();
    LocalSchedulingApiRouter router;
    router.InitUpdateSchedulingStatusHandler(resourceViewMgr);

    auto handlers = router.GetHandlers();
    ASSERT_NE(handlers, nullptr);
    ASSERT_NE(handlers->find("/localschedulingstatus"), handlers->end());

    HttpRequest request;
    request.method = "GET";

    EXPECT_CALL(*resourceViewMgr, UpdateAllUnitStatus(_)).Times(0);

    auto response = handlers->at("/localschedulingstatus")(request);
    ASSERT_AWAIT_READY(response);
    EXPECT_EQ(response.Get().retCode, litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
}

TEST(TcpTunnelServerTest, ResolvesPublishedTCPPort)
{
    std::string error;
    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:30222:22","udp:30223:22"])", 22, error), 30222);
    EXPECT_TRUE(error.empty());

    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:38080:8080"])", 8080, error), 38080);
    EXPECT_TRUE(error.empty());

    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:38081:2222"])", 0, error), 38081);
    EXPECT_TRUE(error.empty());

    EXPECT_EQ(ResolvePublishedTCPPort(R"(["30224:22"])", 22, error), -1);
}

TEST(TcpTunnelServerTest, RejectsInvalidOrNonTcpPortMapping)
{
    std::string error;
    EXPECT_EQ(ResolvePublishedTCPPort(R"(["udp:30223:22"])", 22, error), -1);
    EXPECT_EQ(error, "requested container port is not published as TCP");

    EXPECT_EQ(ResolvePublishedTCPPort(R"({"tcp:30222:22":true})", 22, error), -1);
    EXPECT_EQ(error, "port forward metadata must be an array");

    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:30222:22","tcp:30223:8080"])", 0, error), -1);
    EXPECT_EQ(error, "target port is required when multiple TCP ports are published");

    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:30222:22","tcp:30223:8080"])", 9090, error), -1);
    EXPECT_EQ(error, "requested container port is not published as TCP");

    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:30222abc:22"])", 22, error), -1);
    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp:30222:22abc"])", 22, error), -1);
    EXPECT_EQ(ResolvePublishedTCPPort(R"(["tcp: 30222:22"])", 22, error), -1);
}
}  // namespace functionsystem::test
