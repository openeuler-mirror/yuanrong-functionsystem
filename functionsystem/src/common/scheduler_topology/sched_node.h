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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_TOPOLOGY_SCHED_NODE_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_TOPOLOGY_SCHED_NODE_H

#include <memory>

#include "node.h"

namespace functionsystem {

class SchedNode : public Node, public std::enable_shared_from_this<SchedNode> {
public:
    SchedNode() = delete;
    SchedNode(const NodeInfo &nodeInfo, const int level);
    ~SchedNode() override;

    const NodeInfo &GetNodeInfo() const override;
    Node::TreeNode GetParent() const override;
    const ChildNodes &GetChildren() const override;
    messages::ScheduleTopology GetTopologyView() const override;
    void SetState(const NodeState &state) override;
    const NodeState &GetState() override;
    bool IsLeaf() const override;
    void AddChild(const TreeNode &node) override;
    void SetParent(const TreeNode &node) override;
    void RemoveChild(const std::string &name) override;
    void SetNodeInfo(const NodeInfo &nodeInfo) override;
    bool CheckAddNonLeafNode(size_t maxChildrenNum) override;
    int GetLevel() override;
    bool CheckAddLeafNode(size_t maxChildrenNum) override;

private:
    ChildNodes children_;
    ParentNode parent_;
    NodeInfo nodeInfo_;
    NodeState nodeState_ = NodeState::CONNECTED;
    int level_ = 0;
};

}  // namespace functionsystem

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_TOPOLOGY_SCHED_NODE_H
