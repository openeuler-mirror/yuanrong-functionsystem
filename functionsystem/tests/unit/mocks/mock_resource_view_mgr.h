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

#ifndef TEST_UNIT_MOCK_RESOURCE_VIEW_MGR_H
#define TEST_UNIT_MOCK_RESOURCE_VIEW_MGR_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/resource_view_mgr.h"

namespace functionsystem::test {

using namespace resource_view;
class MockResourceViewMgr : public ResourceViewMgr {
public:
    MockResourceViewMgr() : ResourceViewMgr()
    {
    }

    MOCK_METHOD(std::shared_ptr<ResourceView>, GetInf, (const ResourceType &type), (override));
    MOCK_METHOD((litebus::Future<std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>>>), GetResources, (),
                (override));
    MOCK_METHOD((litebus::Future<std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>>>), GetChanges,
                (), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCK_RESOURCE_VIEW_MGR_H