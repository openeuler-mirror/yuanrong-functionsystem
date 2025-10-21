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

#include "common/scheduler_topology/sched_tree.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace functionsystem::test {

using namespace ::testing;

const size_t TEST_MAX_CHILD_NODE_PER_PARENT_NODE = 2;

class SchedTreeTest : public ::testing::Test {};

/**
 * Feature: Scheduler topology.
 * Description: Add a leaf node but no parent node is available.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a leaf node.
 * Expectation: Failed to add a leaf node, and return nullptr.
 */
TEST_F(SchedTreeTest, AddLeafNodeToEmptyTree)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    EXPECT_TRUE(schedTree.AddLeafNode({ "node", "127.0.0.1:1" }) == nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Add a leaf node, and find a available parent node.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add a leaf node.
 * Expectation: Success to add the leaf node to the non-leaf node.
 */
TEST_F(SchedTreeTest, AddLeafNodeSuccess)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    ASSERT_TRUE(schedTree.AddNonLeafNode({ "parent", "127.0.0.1:1" }) != nullptr);
    EXPECT_TRUE(schedTree.AddLeafNode({ "child", "127.0.0.1:2" }) != nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Add a leaf node but the nodes in the tree are full.
 * Steps:
 * 1. Create an empty tree with maxLocalSchedPerDomainNode_ set to 2.
 * 2. Add three leaf nodes.
 * Expectation: The first two leaf nodes are added successfully, but the last leaf node fails to be added.
 */
TEST_F(SchedTreeTest, AddLeafNodeToFullTree)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    ASSERT_TRUE(schedTree.AddNonLeafNode({ "parent", "127.0.0.1:1" }) != nullptr);
    EXPECT_TRUE(schedTree.AddLeafNode({ "child1", "127.0.0.1:2" }) != nullptr);
    EXPECT_TRUE(schedTree.AddLeafNode({ "child2", "127.0.0.1:3" }) != nullptr);
    EXPECT_TRUE(schedTree.AddLeafNode({ "child3", "127.0.0.1:4" }) == nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Add three non-leaf nodes to a empty tree.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add three non-leaf nodes.
 * Expectation: When the first leaf node is added, it becomes the root node. When the second leaf node is added, the
 * second leaf node becomes the root node, and the first node is a child node of the second node. When the third leaf
 * node is added, it is a child node of the second node.
 */
TEST_F(SchedTreeTest, AddThreeNonLeafNodeToEmptyTree)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    auto node1 = schedTree.AddNonLeafNode({ "node1", "127.0.0.1:1" });
    ASSERT_TRUE(node1 != nullptr);
    EXPECT_TRUE(node1->GetParent() == nullptr);

    auto node2 = schedTree.AddNonLeafNode({ "node2", "127.0.0.1:2" });
    ASSERT_TRUE(node2 != nullptr);
    EXPECT_TRUE(node2->GetParent() == nullptr);
    EXPECT_TRUE(node1->GetParent() == node2);

    auto node3 = schedTree.AddNonLeafNode({ "node3", "127.0.0.1:2" });
    ASSERT_TRUE(node3 != nullptr);
    EXPECT_TRUE(node2->GetParent() == nullptr);
    EXPECT_TRUE(node3->GetParent() == node2);
}

/**
 * Feature: Scheduler topology.
 * Description: Serialize and recover a scheduler tree.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node to the tree.
 * 3. Add two leaf nodes to the tree.
 * 4. Serialize the tree as a string.
 * 5. Recover the tree from the string.
 * Expectation: The recovered tree is same as the tree before serialized.
 */
TEST_F(SchedTreeTest, SerializeAndRecover)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";
    const std::string nodeName2 = "node2";
    const std::string nodeAddress2 = "127.0.0.1:2";
    const std::string nodeName3 = "node3";
    const std::string nodeAddress3 = "127.0.0.1:3";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    auto node1 = schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });
    ASSERT_TRUE(node1 != nullptr);

    auto node2 = schedTree.AddLeafNode({ nodeName2, nodeAddress2 });
    ASSERT_TRUE(node2 != nullptr);

    auto node3 = schedTree.AddLeafNode({ nodeName3, nodeAddress3 });
    ASSERT_TRUE(node3 != nullptr);

    EXPECT_TRUE(node2->GetParent() == node1);
    EXPECT_TRUE(node3->GetParent() == node1);

    auto topologyInfo = schedTree.SerializeAsString();
    SchedTree recoveredTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    recoveredTree.RecoverFromString(topologyInfo);
    EXPECT_EQ(recoveredTree.GetRootNode()->GetNodeInfo().name, nodeName1);
    EXPECT_EQ(recoveredTree.GetRootNode()->GetNodeInfo().address, nodeAddress1);

    auto children = recoveredTree.GetRootNode()->GetChildren();

    ASSERT_TRUE(children.find(nodeName2) != children.end());
    ASSERT_TRUE(children.find(nodeName3) != children.end());

    EXPECT_EQ(children.at(nodeName2)->GetNodeInfo().address, nodeAddress2);
    EXPECT_EQ(children.at(nodeName3)->GetNodeInfo().address, nodeAddress3);

    SchedTree schedTree1(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree1.AddNonLeafNode({ nodeName1, nodeAddress1 });
    schedTree1.AddLeafNode({ nodeName2, nodeAddress2 });
    auto topologyInfo1 = schedTree1.SerializeAsString();
    schedTree.RecoverFromString(topologyInfo1);
    const auto &leafLevel = schedTree.FindNodes(0);
    ASSERT_TRUE(leafLevel.find(nodeName3) == leafLevel.end());
}

/**
 * Feature: Scheduler topology.
 * Description: Replace node in a empty tree.
 * Steps:
 * 1. Create an empty tree.
 * 2. replace a node.
 * Expectation: The replacement fails and return nullptr.
 */
TEST_F(SchedTreeTest, Repalce)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    auto node = schedTree.ReplaceNonLeafNode("node", { "node1", "127.0.0.1:1" });
    EXPECT_TRUE(node == nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Replace a broken node.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add a leaf node.
 * 4. Set state of the non-leaf node to BROKEN.
 * 5. Replace the non-leaf node.
 * Expectation: The broken node is replaced with a new node. And the leaf node's parent changes to the new node.
 */
TEST_F(SchedTreeTest, ReplaceBrokenNode)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    auto node1 = schedTree.AddNonLeafNode({ "node1", "127.0.0.1:1" });
    auto node2 = schedTree.AddLeafNode({ "node2", "127.0.0.1:2" });
    schedTree.SetState(node1, NodeState::BROKEN);
    auto node3 = schedTree.ReplaceNonLeafNode("node1", { "node3", "127.0.0.1:3" });

    EXPECT_EQ(node2->GetParent()->GetNodeInfo().name, node3->GetNodeInfo().name);
    EXPECT_EQ(node2->GetParent()->GetNodeInfo().address, node3->GetNodeInfo().address);
}

/**
 * Feature: Scheduler topology.
 * Description: Not find a broken node can be replaced.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add a leaf node.
 * 4. Replace the non-leaf node.
 * Expectation: Failed to replace the non-leaf node, return nullptr.
 */
TEST_F(SchedTreeTest, NoBrokenNodeToReplace)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);

    auto node1 = schedTree.AddNonLeafNode({ "node1", "127.0.0.1:1" });
    auto node2 = schedTree.AddLeafNode({ "node2", "127.0.0.1:2" });
    auto node3 = schedTree.ReplaceNonLeafNode("node1", { "node3", "127.0.0.1:3" });
    EXPECT_TRUE(node3 == nullptr);
    EXPECT_EQ(node2->GetParent()->GetNodeInfo().name, node1->GetNodeInfo().name);
    EXPECT_EQ(node2->GetParent()->GetNodeInfo().address, node1->GetNodeInfo().address);
}

/**
 * Feature: Scheduler topology.
 * Description: Succeed to find a node in the scheduler tree.
 * Steps:
 * 1. Add a non-leaf node to the tree.
 * 2. Add a leaf node to the tree.
 * 3. Find the non-leaf node and the leaf node.
 * Expectation: The correct node information is obtained.
 */
TEST_F(SchedTreeTest, FindNodeSuccess)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";
    const std::string nodeName2 = "node2";
    const std::string nodeAddress2 = "127.0.0.1:2";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });
    schedTree.AddLeafNode({ nodeName2, nodeAddress2 });

    auto node1 = schedTree.FindNonLeafNode(nodeName1);
    ASSERT_TRUE(node1 != nullptr);
    EXPECT_EQ(node1->GetNodeInfo().address, nodeAddress1);
    auto node2 = schedTree.FindLeafNode(nodeName2);
    ASSERT_TRUE(node2 != nullptr);
    EXPECT_EQ(node2->GetNodeInfo().address, nodeAddress2);
}

/**
 * Feature: Scheduler topology.
 * Description: Failed to find a node in the scheduler tree.
 * Steps:
 * 1. Add a non-leaf node to the tree.
 * 2. Find a leaf node.
 * Expectation: Not found the node.
 */
TEST_F(SchedTreeTest, FindNodeFail)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";
    const std::string nodeName2 = "node2";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });

    auto node2 = schedTree.FindNonLeafNode(nodeName2);
    EXPECT_EQ(node2, nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Remove leaf node in an empty tree.
 * Steps:
 * 1. Create an empty tree.
 * 2. Remove a leaf node.
 * Expectation: Failed to remove and return nullptr.
 */
TEST_F(SchedTreeTest, RemoveLeafNodeInEmptyTree)
{
    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    EXPECT_EQ(schedTree.RemoveLeafNode("node"), nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Remove a node that does not exist.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add a leaf node.
 * 4. Remove a node not exist.
 * Expectation: Failed to remove the node and return nullptr.
 */
TEST_F(SchedTreeTest, RemoveLeafNodeNotExist)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";
    const std::string nodeName2 = "node2";
    const std::string nodeAddress2 = "127.0.0.1:2";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });
    schedTree.AddLeafNode({ nodeName2, nodeAddress2 });

    EXPECT_EQ(schedTree.RemoveLeafNode("node"), nullptr);
}

/**
 * Feature: Scheduler topology.
 * Description: Remove an existing node.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add a leaf node.
 * 4. Remove the leaf node.
 * Expectation: Succeed to remove the leaf node and return the its parent node. The number of child nodes of the parent
 * node needs to be reduced by one.
 */
TEST_F(SchedTreeTest, RemoveLeafNodeSuccess)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";
    const std::string nodeName2 = "node2";
    const std::string nodeAddress2 = "127.0.0.1:2";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });
    schedTree.AddLeafNode({ nodeName2, nodeAddress2 });

    auto node = schedTree.RemoveLeafNode(nodeName2);
    ASSERT_TRUE(node != nullptr);
    EXPECT_EQ(node->GetNodeInfo().name, nodeName1);
    EXPECT_EQ(node->GetNodeInfo().address, nodeAddress1);
    EXPECT_EQ(node->GetChildren().size(), static_cast<uint32_t>(0));
}

/**
 * Feature: Scheduler topology.
 * Description: Add same leaf node to topology tree.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add a leaf node.
 * 4. Add the leaf node again.
 * Expectation: The leaf node is added only once, and the node added for the first time is returned.
 */
TEST_F(SchedTreeTest, AddSameLeafNode)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";
    const std::string nodeName2 = "node2";
    const std::string nodeAddress2 = "127.0.0.1:2";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });
    schedTree.AddLeafNode({ nodeName2, nodeAddress2 });
    auto node = schedTree.AddLeafNode({ nodeName2, nodeAddress2 });

    ASSERT_TRUE(node != nullptr);
    EXPECT_EQ(node->GetNodeInfo().name, nodeName2);
    EXPECT_EQ(node->GetNodeInfo().address, nodeAddress2);
    EXPECT_EQ(node->GetChildren().size(), static_cast<uint32_t>(0));
}

/**
 * Feature: Scheduler topology.
 * Description: Add same non-leaf node to topology tree.
 * Steps:
 * 1. Create an empty tree.
 * 2. Add a non-leaf node.
 * 3. Add the non-leaf node again.
 * Expectation: The non-leaf node is added only once, and the node added for the first time is returned.
 */
TEST_F(SchedTreeTest, AddSameNoLeafNode)
{
    const std::string nodeName1 = "node1";
    const std::string nodeAddress1 = "127.0.0.1:1";

    SchedTree schedTree(TEST_MAX_CHILD_NODE_PER_PARENT_NODE, TEST_MAX_CHILD_NODE_PER_PARENT_NODE);
    schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });
    auto node = schedTree.AddNonLeafNode({ nodeName1, nodeAddress1 });

    ASSERT_TRUE(node != nullptr);
    EXPECT_EQ(node->GetNodeInfo().name, nodeName1);
    EXPECT_EQ(node->GetNodeInfo().address, nodeAddress1);
    EXPECT_EQ(node->GetChildren().size(), static_cast<uint32_t>(0));
}
}  // namespace functionsystem::test
