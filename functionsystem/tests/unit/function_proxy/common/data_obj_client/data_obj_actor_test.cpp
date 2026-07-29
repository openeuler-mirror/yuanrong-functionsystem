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

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "function_proxy/common/data_obj_client/data_obj_actor.h"
#include "mocks/mock_distributed_cache_client.h"

namespace functionsystem::test {
using ::testing::_;
using ::testing::Invoke;

TEST(DataObjActorTest, GetObjNodeListSortsLocationsWithoutRouterLookup)
{
    auto cacheClient = std::make_shared<MockDistributedCacheClient>();
    std::vector<ObjMetaInfo> objMetaInfos = {
        { { "7.185.105.37:31501" }, 2000021 },
        { { "7.218.5.63:31501" }, 4000021 },
        { { "7.185.105.37:31501" }, 1000000 },
    };
    EXPECT_CALL(*cacheClient, GetObjMetaInfo("default", _, _))
        .WillOnce(Invoke([&objMetaInfos](const std::string &, const std::vector<std::string> &,
                                         std::vector<ObjMetaInfo> &result) {
            result = objMetaInfos;
            return Status::OK();
        }));
    EXPECT_CALL(*cacheClient, GetWorkerAddrByWorkerId(_, _)).Times(0);

    DataObjActor actor(cacheClient);
    const auto nodes = actor.GetObjNodeList("default", { "obj1", "obj2", "obj3" });

    ASSERT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[0], "7.218.5.63");
    EXPECT_EQ(nodes[1], "7.185.105.37");
}

}  // namespace functionsystem::test
