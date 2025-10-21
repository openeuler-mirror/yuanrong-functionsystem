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

#include "common/meta_store_adapter/meta_store_operate_cacher.h"

namespace functionsystem::test {

class MetaStoreOperateCacherTest : public ::testing::Test {
};

TEST_F(MetaStoreOperateCacherTest, AddAndErasePutEventTest)
{
    MetaStoreOperateCacher cacher;
    std::string prefixKey = "/yr/route";
    std::string key = "test_key";
    std::string value = "test_value";
    std::string newValue = "new_value";
    cacher.AddPutEvent(prefixKey, key, value);
    ASSERT_EQ(cacher.GetPutEventMap()[prefixKey][key], value);

    cacher.AddPutEvent(prefixKey, key, newValue);
    ASSERT_EQ(cacher.GetPutEventMap()[prefixKey][key], newValue);

    cacher.ErasePutEvent(prefixKey, key);
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), true);
}

TEST_F(MetaStoreOperateCacherTest, AddAndEraseDeleteEventTest)
{
    MetaStoreOperateCacher cacher;
    std::string prefixKey = "/yr/route";
    std::string key = "test_key";
    cacher.AddDeleteEvent(prefixKey, key);
    ASSERT_EQ(cacher.GetDeleteEventMap()[prefixKey].size(), static_cast<long unsigned int>(1));
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), false);

    cacher.EraseDeleteEvent(prefixKey, key);
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), true);
}

TEST_F(MetaStoreOperateCacherTest, AddAndEraseMixEventTest)
{
    MetaStoreOperateCacher cacher;
    std::string prefixKey = "/yr/route";
    std::string key = "test_key";
    cacher.AddDeleteEvent(prefixKey, key);
    ASSERT_EQ(cacher.GetDeleteEventMap()[prefixKey].count(key), static_cast<long unsigned int>(1));

    std::string value = "test_value";
    cacher.AddPutEvent(prefixKey, key, value);
    auto putEventMap = cacher.GetPutEventMap();
    ASSERT_EQ(putEventMap.find(prefixKey), putEventMap.end());

    cacher.EraseDeleteEvent(prefixKey, key);
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), true);

    cacher.AddPutEvent(prefixKey, key, value);
    ASSERT_EQ(cacher.GetPutEventMap()[prefixKey].count(key), static_cast<long unsigned int>(1));
}

TEST_F(MetaStoreOperateCacherTest, IsCacheClearTest)
{
    MetaStoreOperateCacher cacher;
    std::string prefixKey = "/yr/route";
    std::string key = "test_key";
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), true);
    std::string value = "test_value";

    cacher.AddPutEvent(prefixKey, key, value);
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), false);

    cacher.AddDeleteEvent(prefixKey, key);
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), false);

    cacher.EraseDeleteEvent(prefixKey, key);
    ASSERT_EQ(cacher.IsCacheClear(prefixKey), false);

}

}  // namespace functionsystem::test
