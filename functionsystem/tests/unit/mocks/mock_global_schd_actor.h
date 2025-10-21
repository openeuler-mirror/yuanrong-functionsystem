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

#ifndef TEST_UNIT_MOCKS_MOCK_GLOBAL_SCHED_ACTOR_H
#define TEST_UNIT_MOCKS_MOCK_GLOBAL_SCHED_ACTOR_H

#include <gmock/gmock.h>

#include "global_sched_actor.h"

namespace functionsystem::test {

class MockGlobalSchedActor : public global_scheduler::GlobalSchedActor {
public:
    MockGlobalSchedActor(const std::string &name, std::shared_ptr<MetaStoreClient> metaStoreClient,
                         std::shared_ptr<global_scheduler::DomainActivator> domainActivator,
                         std::unique_ptr<global_scheduler::Tree> &&topologyTree)
        : global_scheduler::GlobalSchedActor(name, std::move(metaStoreClient), std::move(domainActivator),
                                             std::move(topologyTree))
    {
    }
    MOCK_METHOD(global_scheduler::Node::TreeNode, AddLocalSched, (const std::string &name, const std::string &address),
                (override));
    MOCK_METHOD(global_scheduler::Node::TreeNode, DelLocalSched, (const std::string &name), (override));
    MOCK_METHOD(global_scheduler::Node::TreeNode, AddDomainSched, (const std::string &name, const std::string &address),
                (override));
    MOCK_METHOD(global_scheduler::Node::TreeNode, DelDomainSched, (const std::string &name), (override));
    MOCK_METHOD(Status, RecoverSchedTopology, (), (override));
    MOCK_METHOD(Status, CacheLocalSched,
                (const litebus::AID &from, const std::string &name, const std::string &address), (override));
    MOCK_METHOD(global_scheduler::Node::TreeNode, FindRootDomainSched, (), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_GLOBAL_SCHED_ACTOR_H
