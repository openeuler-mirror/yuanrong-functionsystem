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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_TOPOLOGY_SCHED_TREE_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_TOPOLOGY_SCHED_TREE_H

#include "tree.h"

namespace functionsystem {

class SchedTree : public Tree {
public:
    SchedTree();
    SchedTree(size_t maxLocalSchedPerDomainNode, size_t maxDomainSchedPerDomainNode);
    ~SchedTree() override;
    Node::TreeNode AddLeafNode(const NodeInfo &nodeInfo) override;

    Node::TreeNode AddNonLeafNode(const NodeInfo &nodeInfo) override;

    std::string SerializeAsString() const override;

    Status RecoverFromString(const std::string &topologyInfo) override;

    Node::TreeNode GetRootNode() const override;

    Node::TreeNode ReplaceNonLeafNode(const std::string &replacedNode, const NodeInfo &nodeInfo) override;

    Node::TreeNode FindNonLeafNode(const std::string &name) override;

    Node::TreeNode FindLeafNode(const std::string &name) override;

    Node::TreeNode RemoveLeafNode(const std::string &name) override;

    void SetState(const Node::TreeNode &node, const NodeState &state) override;

    std::unordered_map<std::string, Node::TreeNode> FindNodes(const uint64_t level) override;

private:
    Node::TreeNode AddNode(const NodeInfo &nodeInfo, size_t level);
    google::protobuf::RepeatedPtrField<messages::SchedulerNode> GetChildrenProto(
        const Node::ChildNodes &childNodes) const;
    void AddChildFromProto(const Node::TreeNode &node, const messages::SchedulerNode& proto);

    // levelNodes[0] stores LocalNodes. Others store DomainNodes.
    std::vector<std::unordered_map<std::string, Node::TreeNode>> levelNodes_;
    Node::TreeNode nextParent_;
    size_t maxLocalSchedPerDomainNode_;
    size_t maxDomainSchedPerDomainNode_;
};

}  // namespace functionsystem

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_TOPOLOGY_SCHED_TREE_H
