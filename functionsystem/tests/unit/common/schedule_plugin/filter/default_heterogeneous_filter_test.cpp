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

#include "common/schedule_plugin/filter/default_heterogeneous_filter/default_heterogeneous_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::filter;
using namespace functionsystem::schedule_framework;

class DefaultHeterogeneousFilterTest : public Test {};

// Normal scenario: Valid heterogeneous resource request
TEST(DefaultHeterogeneousFilterTest, ValidHeterogeneousResourceRequest) {
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1);
    auto unit = view_utils::Get1DResourceUnitWithNpu();

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    DefaultHeterogeneousFilter filter;

    // case 1
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(4);
    auto unit2 = view_utils::Get1DResourceUnitWithSpecificNpuNumber({5,0,0,0,100,100,100,100});
    EXPECT_EQ(filter.Filter(preAllocated, instance2, unit2).status, SUCCESS);

    // case 2
    resource_view::Resources rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber({20,20,20,20,20,20,20,20},
                                                                                         {10,10,10,10,10,10,10,10},
                                                                                         {1,1,1,1,1,1,1,1});
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 1);
}

// Abnormal scenario: Invalid context
TEST(DefaultHeterogeneousFilterTest, InvalidContext) {
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1);
    auto unit = view_utils::Get1DResourceUnitWithNpu();

    DefaultHeterogeneousFilter filter;
    auto status = filter.Filter(nullptr, instance, unit);
    EXPECT_EQ(status.status, StatusCode::PARAMETER_ERROR);
    EXPECT_TRUE(status.isFatalErr);
}

// Abnormal scenario: No heterogeneous resource
TEST(DefaultHeterogeneousFilterTest, NoHeterogeneousResource) {
    auto instance = view_utils::Get1DInstance();
    auto unit = view_utils::Get1DResourceUnit();
    auto preAllocated = std::make_shared<PreAllocatedContext>();

    DefaultHeterogeneousFilter filter;
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
}

// Abnormal scenario: Invalid available resource
TEST(DefaultHeterogeneousFilterTest, InvalidAvailableResource) {
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1);
    auto unit = view_utils::Get1DResourceUnitWithSpecificNpuNumber({20,20,20,20,20,20,20,20});

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber(
            {20,20,20,20,20,20,20,20},
            {10,10,10,10,10,10,10,10},
            {1,1,1,1,1,1,1,1},
            resource_view::NPU_RESOURCE_NAME + "/310",
            unit.allocatable().resources().at(resource_view::NPU_RESOURCE_NAME + "/310")
                .vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY).vectors().begin()->first);

    rs.mutable_resources()->at(resource_view::CPU_RESOURCE_NAME).mutable_scalar()->set_value(20);
    rs.mutable_resources()->at(resource_view::MEMORY_RESOURCE_NAME).mutable_scalar()->set_value(20);
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    DefaultHeterogeneousFilter filter;
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, HETEROGENEOUS_SCHEDULE_FAILED);
}

// Abnormal scenario: Request value of 0
TEST(DefaultHeterogeneousFilterTest, RequestValueZero) {
    auto instance = view_utils::Get1DInstanceWithNpuResource(0, 0, 0);
    auto unit = view_utils::Get1DResourceUnitWithNpu();
    auto preAllocated = std::make_shared<PreAllocatedContext>();

    DefaultHeterogeneousFilter filter;
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::PARAMETER_ERROR);
}

// Abnormal scenario: No available heterogeneous resource
TEST(DefaultHeterogeneousFilterTest, NoAvailableHeterogeneousResource) {
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1);
    auto unit = view_utils::Get1DResourceUnitWithSpecificNpuNumber({2,3,4,5,4,4,4,2});
    auto preAllocated = std::make_shared<PreAllocatedContext>();

    DefaultHeterogeneousFilter filter;
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, HETEROGENEOUS_SCHEDULE_FAILED);
}

// Abnormal scenario: Card number request not satisfied
TEST(DefaultHeterogeneousFilterTest, CardNumberRequestNotSatisfied) {
    auto instance = view_utils::Get1DInstanceWithNpuResource(5);
    auto unit = view_utils::Get1DResourceUnitWithSpecificNpuNumber({0,0,0,0,100,100,100,100});
    auto preAllocated = std::make_shared<PreAllocatedContext>();

    // case 1
    DefaultHeterogeneousFilter filter;
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, HETEROGENEOUS_SCHEDULE_FAILED);
    std::cout << filter.Filter(preAllocated, instance, unit).status.GetMessage() << std::endl;

    // case 3
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(4);
    auto unit3 = view_utils::Get1DResourceUnitWithSpecificNpuNumber({5,0,0,0,99,100,100,100});
    EXPECT_EQ(filter.Filter(preAllocated, instance3, unit3).status, HETEROGENEOUS_SCHEDULE_FAILED);
}

// Frac Card number request
TEST(DefaultHeterogeneousFilterTest, FracCardNumberTest) {
    // case 1
    auto instance = view_utils::Get1DInstanceWithNpuResource(0.000001);
    auto unit = view_utils::Get1DResourceUnitWithSpecificNpuNumber({0,0,0,0,100,100,100,100});
    auto preAllocated = std::make_shared<PreAllocatedContext>();

    DefaultHeterogeneousFilter filter;
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, PARAMETER_ERROR);
    std::cout << filter.Filter(preAllocated, instance, unit).status.GetMessage() << std::endl;

    // case 2
    instance = view_utils::Get1DInstanceWithNpuResource(1.01);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, PARAMETER_ERROR);

    // case 3
    instance = view_utils::Get1DInstanceWithNpuResource(0.7);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, SUCCESS);

}

// Regex scenario
TEST(DefaultHeterogeneousFilterTest, ValidHetegroRegeexRequest) {
    DefaultHeterogeneousFilter filter;
    // 1.valid regex
    // request(NPU/Ascend910.*) <--> reousrceview(NPU/Ascend910)
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1, "NPU/Ascend910.*");
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910");

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber({20,20,20,20,20,20,20,20},
                                                                                         {10,10,10,10,10,10,10,10},
                                                                                         {1,1,1,1,1,1,1,1},
                                                                                         "NPU/Ascend910");
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 1);

    // 2.valid regex
    // request(NPU/Ascend910.*) <--> reousrceview(NPU/Ascend910B4)
    instance = view_utils::Get1DInstanceWithNpuResource(3, "NPU/Ascend910.*");
    unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B4");

    preAllocated = std::make_shared<PreAllocatedContext>();
    rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber({20,20,20,20,20,20,20,20},
                                                                 {10,10,10,10,10,10,10,10},
                                                                 {1,1,1,1,1,1,1,1},
                                                                 "NPU/Ascend910B4");
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 1);

    // 3.valid regex
    // request(NPU/.+) <--> reousrceview(NPU/Ascend910B4)
    instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1, "NPU/.+");
    unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B4");

    preAllocated = std::make_shared<PreAllocatedContext>();
    rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber({20,20,20,20,20,20,20,20},
                                                                {10,10,10,10,10,10,10,10},
                                                                {1,1,1,1,1,1,1,1},
                                                                "NPU/Ascend910B4");
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::SUCCESS);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 1);

    // 4.invalid regex
    // request(NPU/Ascend310) <-X-> reousrceview(NPU/Ascend910)
    instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1, "NPU/Ascend310");
    unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910");

    preAllocated = std::make_shared<PreAllocatedContext>();
    rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber({20,20,20,20,20,20,20,20},
                                                                {10,10,10,10,10,10,10,10},
                                                                {1,1,1,1,1,1,1,1},
                                                                "NPU/Ascend910");
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::HETEROGENEOUS_SCHEDULE_FAILED);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);

    // 5.invalid regex
    // request(NPU/^Ascend910.*)
    instance = view_utils::Get1DInstanceWithNpuResource(6, 20, 1, "NPU/^Ascend910.*");
    unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910");

    preAllocated = std::make_shared<PreAllocatedContext>();
    rs = view_utils::GetCpuMemNpuResourcesWithSpecificNpuNumber({20,20,20,20,20,20,20,20},
                                                                {10,10,10,10,10,10,10,10},
                                                                {1,1,1,1,1,1,1,1},
                                                                "NPU/Ascend910");
    preAllocated->allocated[unit.id()].resource = std::move(rs);

    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).status, StatusCode::HETEROGENEOUS_SCHEDULE_FAILED);
    EXPECT_EQ(filter.Filter(preAllocated, instance, unit).availableForRequest, 0);
}

}  // namespace functionsystem::test