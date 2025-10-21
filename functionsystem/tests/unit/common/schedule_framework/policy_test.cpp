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
#include "common/scheduler_framework/framework/policy.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"

namespace functionsystem::test {

using namespace ::testing;
class PolicyInterfaceTest : public Test {};

/**
 * Description: Test ProtoMapPreFilterResultTest
 * Steps:
 * 1. input empty Map and RESOURCE_NOT_ENOUGH  -> return RESOURCE_NOT_ENOUGH
 * 2. input a Map and Status::OK  -> return ProtoMapPreFilterResult with right size and key
 */
TEST_F(PolicyInterfaceTest, ProtoMapPreFilterResultTest)
{
    ::google::protobuf::Map<std::string, resource_view::ResourceUnit> testUnitMap;
    ::google::protobuf::Map<std::string, resource_view::BucketInfo> testBucketMap;

    // test for map is empty
    {
        auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::ResourceUnit>>(
            testUnitMap, Status{ StatusCode::RESOURCE_NOT_ENOUGH, "no node is available" });
        EXPECT_TRUE(res->empty());
        EXPECT_EQ(res->status().StatusCode(), StatusCode::RESOURCE_NOT_ENOUGH);
    }

    std::set<std::string>  keyList = { "key1", "key2", "key3" };
    for (const auto &item : keyList) {
        testUnitMap[item] = resource_view::ResourceUnit();
        testBucketMap[item] = resource_view::BucketInfo();
    }
    // test for map<string, ResourceUnit>
    {
        auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::ResourceUnit>>(
            testUnitMap, Status::OK());
        int cnt = 0;
        EXPECT_TRUE(res->status().IsOk());
        while (!res->end()) {
            EXPECT_TRUE(keyList.find(res->current()) != keyList.end());
            cnt++;
            res->next();
        }
        EXPECT_TRUE(cnt == static_cast<int>(keyList.size()));
    }

    // test for map<string, resource_view::BucketInfo>
    {
        auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::BucketInfo>>(
            testBucketMap, Status::OK());
        int cnt = 0;
        EXPECT_TRUE(res->status().IsOk());
        while (!res->end()) {
            EXPECT_TRUE(keyList.find(res->current()) != keyList.end());
            cnt++;
            res->next();
        }
        EXPECT_TRUE(cnt == static_cast<int>(keyList.size()));
    }
}

TEST_F(PolicyInterfaceTest, SetPreFilterResultTest)
{
    std::set<std::string> testSet;
    // test for set is empty
    {
        auto res = std::make_shared<schedule_framework::SetPreFilterResult>(
            testSet, Status{ StatusCode::RESOURCE_NOT_ENOUGH, "no node is available" });
        EXPECT_TRUE(res->empty());
        EXPECT_EQ(res->status().StatusCode(), StatusCode::RESOURCE_NOT_ENOUGH);
    }

    std::set<std::string>  keyList = { "key1", "key2", "key3" };
    for (const auto &item : keyList) {
        testSet.insert(item);
    }
    // test for set is not empty
    {
        auto res = std::make_shared<schedule_framework::SetPreFilterResult>(testSet, Status::OK());
        int cnt = 0;
        EXPECT_TRUE(res->status().IsOk());
        while (!res->end()) {
            EXPECT_TRUE(keyList.find(res->current()) != keyList.end());
            cnt++;
            res->next();
        }
        EXPECT_TRUE(cnt == static_cast<int>(keyList.size()));
    }

}

/*
 * Valid key triggers cyclic iteration:
 * - Reset shifts iteration start to target's next element
 * - Full cycle = elements after target + elements up to target
 */
TEST_F(PolicyInterfaceTest, ResetValidKeyWithLoop) {
    ::google::protobuf::Map<std::string, resource_view::ResourceUnit> testMap;
    testMap["a"] = resource_view::ResourceUnit();
    testMap["b"] = resource_view::ResourceUnit();
    testMap["c"] = resource_view::ResourceUnit();

    auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::ResourceUnit>>(
        testMap, Status::OK());

    // Capture true iteration order through ProtoMapPreFilterResult traversal
    std::vector<std::string> originOrder;
    for (const auto &pair: testMap) {
        originOrder.push_back(pair.first);
    }

    // Generate expected cyclic sequence: elements after target + elements before target
    const std::string targetKey = originOrder[0];
    std::vector<std::string> expectedOrder;
    expectedOrder.push_back(originOrder[1]);
    expectedOrder.push_back(originOrder[2]);
    expectedOrder.push_back(originOrder[0]);

    // Verify post-reset order
    res->reset(targetKey);
    std::vector<std::string> actualOrder;
    while (!res->end()) {
        actualOrder.push_back(res->current());
        res->next();
    }

    EXPECT_EQ(actualOrder, expectedOrder);
}

// Invalid key reset preserves original iteration order
TEST_F(PolicyInterfaceTest, ResetInvalidKey) {
    ::google::protobuf::Map<std::string, resource_view::BucketInfo> testMap;
    testMap["a"] = resource_view::BucketInfo();
    testMap["b"] = resource_view::BucketInfo();

    auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::BucketInfo>>(
        testMap, Status::OK());

    std::vector<std::string> originOrder;
    for (const auto &pair: testMap) {
        originOrder.push_back(pair.first);
    }

    // Invalid reset should maintain original order
    res->reset("invalid_key");
    std::vector<std::string> actualOrder;
    while (!res->end()) {
        actualOrder.push_back(res->current());
        res->next();
    }

    ASSERT_EQ(actualOrder, originOrder);
}

// Reset on last element restarts iteration from first element
TEST_F(PolicyInterfaceTest, ResetAtEndLoop) {
    ::google::protobuf::Map<std::string, resource_view::ResourceUnit> testMap;
    testMap["a"] = resource_view::ResourceUnit();
    testMap["b"] = resource_view::ResourceUnit();
    testMap["c"] = resource_view::ResourceUnit();

    auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::ResourceUnit>>(
        testMap, Status::OK());

    std::vector<std::string> originOrder;
    for (const auto &pair: testMap) {
        originOrder.push_back(pair.first);
    }

    // Reset to last element should cycle to start
    res->reset(originOrder.back());
    ASSERT_FALSE(res->end());
    EXPECT_EQ(res->current(), originOrder.front());

    // Verify full cycle matches original order
    std::vector<std::string> loopOrder;
    for (; !res->end(); res->next()) {
        loopOrder.push_back(res->current());
    }
    ASSERT_EQ(loopOrder, originOrder);
}

// Single-element map maintains stability after reset
TEST_F(PolicyInterfaceTest, SingleElementReset) {
    ::google::protobuf::Map<std::string, resource_view::ResourceUnit> testMap;
    testMap["only_key"] = resource_view::ResourceUnit();

    auto res = std::make_shared<schedule_framework::ProtoMapPreFilterResult<resource_view::ResourceUnit>>(
        testMap, Status::OK());

    // Reset validation
    res->reset("only_key");
    ASSERT_FALSE(res->end());
    EXPECT_EQ(res->current(), "only_key");
}
}  // namespace functionsystem::test