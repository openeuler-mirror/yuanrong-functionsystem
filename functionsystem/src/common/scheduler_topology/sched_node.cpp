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

#include "sched_node.h"

namespace functionsystem {

SchedNode::SchedNode(const NodeInfo &nodeInfo, const int level) : nodeInfo_(nodeInfo), level_(level)
{
}

SchedNode::~SchedNode()
{
    children_.clear();
}

const NodeInfo &SchedNode::GetNodeInfo() const
{
    return nodeInfo_;
}

Node::TreeNode SchedNode::GetParent() const
{
    if (parent_.expired()) {
        return nullptr;
    }
    return parent_.lock();
}

const Node::ChildNodes &SchedNode::GetChildren() const
{
    return children_;
}

messages::ScheduleTopology SchedNode::GetTopologyView() const
{
    auto parent = parent_.lock();
    messages::ScheduleTopology scheduleTopology;
    if (parent != nullptr) {
        scheduleTopology.mutable_leader()->set_name(parent->GetNodeInfo().name);
        scheduleTopology.mutable_leader()->set_address(parent->GetNodeInfo().address);
    }

    for (const auto &child : children_) {
        auto member = scheduleTopology.add_members();
        member->set_name(child.second->GetNodeInfo().name);
        member->set_address(child.second->GetNodeInfo().address);
    }
    return scheduleTopology;
}

void SchedNode::SetState(const NodeState &state)
{
    nodeState_ = state;
}

const NodeState &SchedNode::GetState()
{
    return nodeState_;
}

bool SchedNode::IsLeaf() const
{
    return level_ == 0;
}

void SchedNode::AddChild(const TreeNode &node)
{
    children_[node->GetNodeInfo().name] = node;
    node->SetParent(shared_from_this());
}

void SchedNode::SetParent(const Node::TreeNode &node)
{
    parent_ = node;
}

void SchedNode::SetNodeInfo(const NodeInfo &nodeInfo)
{
    nodeInfo_ = nodeInfo;
}

bool SchedNode::CheckAddNonLeafNode(size_t maxChildrenNum)
{
    return level_ > 1 && GetChildren().size() < maxChildrenNum;
}

int SchedNode::GetLevel()
{
    return level_;
}

bool SchedNode::CheckAddLeafNode(size_t maxChildrenNum)
{
    return level_ == 1 && GetChildren().size() < maxChildrenNum;
}

void SchedNode::RemoveChild(const std::string &name)
{
    (void)children_.erase(name);
}

}  // namespace functionsystem