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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_TREE_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_TREE_H

#include <memory>
#include <unordered_map>

#include "common/status/status.h"
#include "node.h"

namespace functionsystem {

const int DEFAULT_TREE_LEVEL = 2;

class Tree {
public:
    Tree() = default;
    virtual ~Tree() = default;

    /**
     * Add a leaf node to the tree.
     * @param nodeInfo The node info.
     * @return The leaf node has been added.
     */
    virtual Node::TreeNode AddLeafNode(const NodeInfo &nodeInfo) = 0;

    /**
     * Add a non-leaf node to the tree.
     * @param nodeInfo The node info.
     * @return The non-leaf node has been added.
     */
    virtual Node::TreeNode AddNonLeafNode(const NodeInfo &nodeInfo) = 0;

    /**
     * Serialize tree topology as a string.
     * @return The topology info.
     */
    virtual std::string SerializeAsString() const = 0;

    /**
     * Recover the tree from a string contains topology info.
     * @return Recover result.
     */
    virtual Status RecoverFromString(const std::string &topologyInfo) = 0;

    /**
     * Get the root node of the topology tree.
     * @return The root node.
     */
    virtual Node::TreeNode GetRootNode() const = 0;

    /**
     * Replace the old node in the tree with the new node information.
     * @return The new node.
     */
    virtual Node::TreeNode ReplaceNonLeafNode(const std::string &replacedNode, const NodeInfo &nodeInfo) = 0;

    /**
     * Set the state of node.
     * @param node The node to change state.
     * @param state The state to which the node will be set.
     */
    virtual void SetState(const Node::TreeNode &node, const NodeState &state) = 0;

    /**
     * Find a tree's non-leaf node according to the name.
     * @param name The name identifier of the node.
     * @return The found tree node.
     */
    virtual Node::TreeNode FindNonLeafNode(const std::string &name) = 0;

    /**
     * Find a tree's leaf node according to the name.
     * @param name The name identifier of the node.
     * @return The found tree node.
     */
    virtual Node::TreeNode FindLeafNode(const std::string &name) = 0;

    /**
     * Remove a leaf node from a tree.
     * @param name The name identifier of the node to remove.
     * @return The parent node of the removed node.
     */
    virtual Node::TreeNode RemoveLeafNode(const std::string &name) = 0;

    /**
     * Find nodes according to level.
     * @param level The level in the scheduler tree.
     * @return The nodes.
     */
    virtual std::unordered_map<std::string, Node::TreeNode> FindNodes(const uint64_t level) = 0;
};

}  // namespace functionsystem

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_TREE_H
