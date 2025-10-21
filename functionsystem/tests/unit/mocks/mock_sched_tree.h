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

#ifndef TEST_UNIT_MOCKS_MOCK_SCHED_TREE_H
#define TEST_UNIT_MOCKS_MOCK_SCHED_TREE_H

#include <gmock/gmock.h>

#include "common/scheduler_topology/sched_tree.h"

namespace functionsystem::test {

class MockSchedTree : public SchedTree {
public:
    MockSchedTree() : SchedTree(2, 2)
    {
    }
    MockSchedTree(const int maxLocalSchedPerDomainNode, const int maxDomainSchedPerDomainNode)
        : SchedTree(maxLocalSchedPerDomainNode, maxDomainSchedPerDomainNode)
    {
    }
    MOCK_METHOD(Node::TreeNode, AddLeafNode, (const NodeInfo &nodeInfo), (override));
    MOCK_METHOD(Node::TreeNode, AddNonLeafNode, (const NodeInfo &nodeInfo), (override));
    MOCK_METHOD(Node::TreeNode, RemoveLeafNode, (const std::string &name), (override));
    MOCK_METHOD(std::string, SerializeAsString, (), (const, override));
    MOCK_METHOD(Status, RecoverFromString, (const std::string &topologyInfo), (override));
    MOCK_METHOD(Node::TreeNode, GetRootNode, (), (const, override));
    MOCK_METHOD(Node::TreeNode, FindNonLeafNode, (const std::string &name), (override));
    MOCK_METHOD(void, SetState, (const Node::TreeNode &node, const NodeState &state), (override));
    MOCK_METHOD(Node::TreeNode, FindLeafNode, (const std::string &name), (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_SCHED_TREE_H
