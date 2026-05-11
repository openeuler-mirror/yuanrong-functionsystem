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

#include <map>

#include "function_master/instance_manager/instance_manager_driver.h"

namespace functionsystem::instance_manager::test {

TEST(InstanceManagerDriverTest, ParseQueryInstancesPaginationDefaultsMissingValues)
{
    std::map<std::string, std::string> query = {{"page", "2"}};

    auto pagination = ParseQueryInstancesPagination(query);

    EXPECT_TRUE(pagination.enabled);
    EXPECT_EQ(pagination.page, 2U);
    EXPECT_EQ(pagination.pageSize, 10U);
    EXPECT_TRUE(pagination.error.empty());
}

TEST(InstanceManagerDriverTest, ParseQueryInstancesPaginationRejectsInvalidValues)
{
    std::map<std::string, std::string> query = {{"page", "0"}, {"page_size", "10"}};

    auto pagination = ParseQueryInstancesPagination(query);

    EXPECT_TRUE(pagination.enabled);
    EXPECT_EQ(pagination.error, "page must be a positive integer");
}

TEST(InstanceManagerDriverTest, ParseQueryInstancesPaginationRejectsOversizedPageSize)
{
    std::map<std::string, std::string> query = {{"page", "1"}, {"page_size", "1001"}};

    auto pagination = ParseQueryInstancesPagination(query);

    EXPECT_TRUE(pagination.enabled);
    EXPECT_EQ(pagination.error, "page_size exceeds maximum limit");
}

TEST(InstanceManagerDriverTest, CollectSortedTenantInstanceIndexesSortsAfterFiltering)
{
    messages::QueryInstancesInfoResponse response;
    auto *third = response.add_instanceinfos();
    third->set_instanceid("instance-c");
    third->set_tenantid("tenant-a");
    auto *first = response.add_instanceinfos();
    first->set_instanceid("instance-a");
    first->set_tenantid("tenant-a");
    auto *otherTenant = response.add_instanceinfos();
    otherTenant->set_instanceid("instance-b");
    otherTenant->set_tenantid("tenant-b");

    auto matched = CollectSortedTenantInstanceIndexes(response.instanceinfos(), "tenant-a", "", false);

    ASSERT_EQ(matched.size(), 2U);
    EXPECT_EQ(response.instanceinfos().Get(matched[0]).instanceid(), "instance-a");
    EXPECT_EQ(response.instanceinfos().Get(matched[1]).instanceid(), "instance-c");
}

TEST(InstanceManagerDriverTest, CollectSortedTenantInstanceIndexesFiltersByInstanceID)
{
    messages::QueryInstancesInfoResponse response;
    auto *second = response.add_instanceinfos();
    second->set_instanceid("instance-b");
    second->set_tenantid("tenant-a");
    auto *first = response.add_instanceinfos();
    first->set_instanceid("instance-a");
    first->set_tenantid("tenant-a");

    auto matched = CollectSortedTenantInstanceIndexes(response.instanceinfos(), "tenant-a", "instance-b", false);

    ASSERT_EQ(matched.size(), 1U);
    EXPECT_EQ(response.instanceinfos().Get(matched[0]).instanceid(), "instance-b");
}

TEST(InstanceManagerDriverTest, CollectSortedTenantInstanceIndexesBreaksInstanceIDTiesByTenantID)
{
    messages::QueryInstancesInfoResponse response;
    auto *second = response.add_instanceinfos();
    second->set_instanceid("instance-a");
    second->set_tenantid("tenant-b");
    auto *first = response.add_instanceinfos();
    first->set_instanceid("instance-a");
    first->set_tenantid("tenant-a");

    auto matched = CollectSortedTenantInstanceIndexes(response.instanceinfos(), "system", "", true);

    ASSERT_EQ(matched.size(), 2U);
    EXPECT_EQ(response.instanceinfos().Get(matched[0]).tenantid(), "tenant-a");
    EXPECT_EQ(response.instanceinfos().Get(matched[1]).tenantid(), "tenant-b");
}

TEST(InstanceManagerDriverTest, GetQueryInstancesPageRangeCalculatesBoundedRanges)
{
    QueryInstancesPagination pagination;
    pagination.enabled = true;
    pagination.page = 2;
    pagination.pageSize = 2;

    auto range = GetQueryInstancesPageRange(5, pagination);

    EXPECT_EQ(range.start, 2U);
    EXPECT_EQ(range.end, 4U);

    pagination.page = 4;
    range = GetQueryInstancesPageRange(5, pagination);

    EXPECT_EQ(range.start, 5U);
    EXPECT_EQ(range.end, 5U);
}

}  // namespace functionsystem::instance_manager::test
