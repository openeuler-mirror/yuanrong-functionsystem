/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
*/

#ifndef UNIT_MOCKS_MOCK_RESOURCE_GROUP_CTRL_H
#define UNIT_MOCKS_MOCK_RESOURCE_GROUP_CTRL_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "local_scheduler/resource_group_controller/resource_group_ctrl.h"

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
class MockResourceGroupCtrl : public ResourceGroupCtrl {
public:
    MockResourceGroupCtrl() : ResourceGroupCtrl(nullptr)
    {
    }
    ~MockResourceGroupCtrl() override = default;

    MOCK_METHOD(litebus::Future<std::shared_ptr<CreateResourceGroupResponse>>, Create,
                (const std::string &from, const std::shared_ptr<CreateResourceGroupRequest> &req), (override));
    MOCK_METHOD(litebus::Future<KillResponse>, Kill,
                (const std::string &from, const std::string &srcTenantID, const std::shared_ptr<KillRequest> &killReq),
                (override));
};
}  // namespace functionsystem::test

#endif  // UNIT_MOCKS_MOCK_RESOURCE_GROUP_CTRL_H
