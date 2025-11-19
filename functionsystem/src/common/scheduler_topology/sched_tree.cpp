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

#include "sched_tree.h"

#include "common/logs/logging.h"
#include "sched_node.h"

namespace functionsystem {

constexpr int MIN_TREE_LEVEL = 2;

SchedTree::SchedTree() : maxLocalSchedPerDomainNode_(0), maxDomainSchedPerDomainNode_(0)
{
}

SchedTree::SchedTree(size_t maxLocalSchedPerDomainNode, size_t maxDomainSchedPerDomainNode)
    : maxLocalSchedPerDomainNode_(maxLocalSchedPerDomainNode), maxDomainSchedPerDomainNode_(maxDomainSchedPerDomainNode)
{
}

SchedTree::~SchedTree()
{
    levelNodes_.clear();
    nextParent_ = nullptr;
}

Node::TreeNode SchedTree::AddLeafNode(const NodeInfo &nodeInfo)
{
    YRLOG_INFO("add leaf node[name: {}, address: {}]", nodeInfo.name, nodeInfo.address);
    if (levelNodes_.size() < MIN_TREE_LEVEL) {
        YRLOG_WARN("failed to add leaf node {}-{}, scheduler tree level is less than {}", nodeInfo.name,
                   nodeInfo.address, MIN_TREE_LEVEL);
        return nullptr;
    }
    const auto &leafLevel = FindNodes(0);
    if (leafLevel.find(nodeInfo.name) != leafLevel.end()) {
        YRLOG_INFO("node[name: {}, address: {}] already in topology tree, update it", nodeInfo.name, nodeInfo.address);
        leafLevel.at(nodeInfo.name)->SetNodeInfo(nodeInfo);
        return leafLevel.at(nodeInfo.name);
    }
    Node::TreeNode domainNode = nullptr;
    // Traverse the DomainSchedulers in the first level.
    for (const auto &it : levelNodes_[1]) {
        if (!it.second->CheckAddLeafNode(maxLocalSchedPerDomainNode_)) {
            continue;
        }
        // Find a DomainScheduler can add node.
        domainNode = it.second;
        break;
    }
    if (domainNode == nullptr) {
        YRLOG_INFO("didn't find a domain node to add local node");
        return nullptr;
    }
    auto localNode = AddNode(nodeInfo, 0);
    domainNode->AddChild(localNode);
    return localNode;
}

Node::TreeNode SchedTree::AddNonLeafNode(const NodeInfo &nodeInfo)
{
    YRLOG_INFO("add non-leaf node[name: {}, address: {}]", nodeInfo.name, nodeInfo.address);
    auto levelSize = levelNodes_.size();
    for (auto level = levelSize > 0 ? levelSize - 1 : 0; level > 0; level--) {
        std::unordered_map<std::string, Node::TreeNode> nodeLevel = FindNodes(level);
        if (nodeLevel.find(nodeInfo.name) != nodeLevel.end()) {
            YRLOG_INFO("node[name: {}, address: {}] already in topology tree, level: {}", nodeInfo.name,
                       nodeInfo.address, level);
            return nodeLevel.at(nodeInfo.name);
        }
    }
    Node::TreeNode node;
    // Next parent is null indicates that no domain node exists.
    if (nextParent_ == nullptr) {
        node = AddNode(nodeInfo, 1);
        nextParent_ = node;
        return node;
    }

    // Find a domain node to which can add sub-nodes until to the root domain node.
    while (!nextParent_->CheckAddNonLeafNode(maxDomainSchedPerDomainNode_) && nextParent_->GetParent() != nullptr) {
        nextParent_ = nextParent_->GetParent();
    }

    if (nextParent_->CheckAddNonLeafNode(maxDomainSchedPerDomainNode_)) {
        int childLevel = nextParent_->GetLevel() - 1;
        node = AddNode(nodeInfo, static_cast<unsigned short>(childLevel));
        nextParent_->AddChild(node);
        if (node == nullptr) {
            return node;
        }
        if (node->CheckAddNonLeafNode(maxDomainSchedPerDomainNode_)) {
            nextParent_ = node;
        }
        return node;
    }

    // If the root domain node can't add sub-node, new node becomes the root domain node.
    int newParentLevel = nextParent_->GetLevel() + 1;
    node = AddNode(nodeInfo, static_cast<unsigned short>(newParentLevel));
    if (node == nullptr) {
        return node;
    }
    node->AddChild(nextParent_);
    nextParent_ = node;
    return node;
}

std::string SchedTree::SerializeAsString() const
{
    messages::SchedulerNode root;
    auto rootNode = GetRootNode();
    if (rootNode == nullptr) {
        return "";
    }
    root.set_name(rootNode->GetNodeInfo().name);
    root.set_address(rootNode->GetNodeInfo().address);
    root.set_level(rootNode->GetLevel());
    root.mutable_children()->CopyFrom(GetChildrenProto(rootNode->GetChildren()));

    return root.SerializeAsString();
}

Status SchedTree::RecoverFromString(const std::string &topologyInfo)
{
    messages::SchedulerNode root;
    if (!root.ParseFromString(topologyInfo)) {
        return Status(StatusCode::GS_SCHED_TOPOLOGY_BROKEN);
    }
    if (root.level() < 0) {
        YRLOG_ERROR("root node's level {} is less than zero", root.level());
        return Status(StatusCode::FAILED);
    }
    size_t level = static_cast<unsigned short>(root.level());

    YRLOG_INFO("add root node[name: {}, address: {}, level: {}]", root.name(), root.address(), level);
    auto rootNode = std::make_shared<SchedNode>(NodeInfo{ root.name(), root.address() }, level);
    levelNodes_.clear();
    levelNodes_.resize(level + 1);
    levelNodes_[level][root.name()] = rootNode;
    AddChildFromProto(rootNode, root);

    return Status::OK();
}

Node::TreeNode SchedTree::GetRootNode() const
{
    if (levelNodes_.size() < MIN_TREE_LEVEL) {
        return nullptr;
    }
    return levelNodes_.back().begin()->second;
}

Node::TreeNode SchedTree::ReplaceNonLeafNode(const std::string &replacedNode, const NodeInfo &nodeInfo)
{
    if (levelNodes_.size() < MIN_TREE_LEVEL) {
        return nullptr;
    }

    for (size_t i = levelNodes_.size() - 1; i > 0; i--) {
        if (levelNodes_[i].find(replacedNode) != levelNodes_[i].end()) {
            YRLOG_INFO("find node {} in level {}", replacedNode, i);
            auto replaced = levelNodes_[i].at(replacedNode);
            if (replaced->GetState() != NodeState::BROKEN) {
                YRLOG_WARN("node {} is not broken, can't be replaced", replacedNode);
                break;
            }
            YRLOG_INFO("replace node[name: {}, address: {}] with node[name: {}, address: {}]",
                       replaced->GetNodeInfo().name, replaced->GetNodeInfo().address, nodeInfo.name, nodeInfo.address);
            replaced->SetNodeInfo(nodeInfo);
            replaced->SetState(NodeState::CONNECTED);
            return replaced;
        }
    }
    YRLOG_DEBUG("didn't find replaced node {}", replacedNode);
    return nullptr;
}

void SchedTree::SetState(const Node::TreeNode &node, const NodeState &state)
{
    node->SetState(state);
}

Node::TreeNode SchedTree::FindNonLeafNode(const std::string &name)
{
    auto levelSize = levelNodes_.size();
    if (levelSize == 0) {
        return nullptr;
    }
    for (auto level = levelSize - 1; level > 0; level--) {
        if (levelNodes_[level].find(name) != levelNodes_[level].end()) {
            return levelNodes_[level].at(name);
        }
    }
    return nullptr;
}

Node::TreeNode SchedTree::FindLeafNode(const std::string &name)
{
    auto levelSize = levelNodes_.size();
    if (levelSize == 0) {
        return nullptr;
    }
    if (levelNodes_[0].find(name) != levelNodes_[0].end()) {
        return levelNodes_[0].at(name);
    }
    return nullptr;
}

Node::TreeNode SchedTree::RemoveLeafNode(const std::string &name)
{
    if (levelNodes_.empty()) {
        YRLOG_WARN("scheduler tree is empty");
        return nullptr;
    }
    auto &localNodes = levelNodes_[0];
    if (localNodes.find(name) == localNodes.end()) {
        YRLOG_WARN("didn't find node {}", name);
        return nullptr;
    }
    const auto &parent = localNodes.at(name)->GetParent();
    if (parent == nullptr) {
        YRLOG_WARN("didn't find parent for node {}", name);
        return nullptr;
    }
    parent->RemoveChild(name);
    (void)localNodes.erase(name);
    return parent;
}

Node::TreeNode SchedTree::AddNode(const NodeInfo &nodeInfo, const size_t level)
{
    YRLOG_DEBUG("add node[name: {}, address: {}] in level {}", nodeInfo.name, nodeInfo.address, level);
    auto node = std::make_shared<SchedNode>(nodeInfo, level);
    if (node == nullptr) {
        YRLOG_ERROR("failed to create a new node[name: {}, address: {}]", nodeInfo.name, nodeInfo.address);
        return nullptr;
    }
    if (levelNodes_.size() < level + 1) {
        YRLOG_INFO("resize tree's height to {}", level + 1);
        levelNodes_.resize(level + 1);
    }
    levelNodes_[level][nodeInfo.name] = node;
    return node;
}

google::protobuf::RepeatedPtrField<messages::SchedulerNode> SchedTree::GetChildrenProto(
    const Node::ChildNodes &childNodes) const
{
    google::protobuf::RepeatedPtrField<messages::SchedulerNode> schedulerNodes;
    for (const auto &child : childNodes) {
        messages::SchedulerNode node;
        node.set_name(child.second->GetNodeInfo().name);
        node.set_address(child.second->GetNodeInfo().address);
        node.set_level(child.second->GetLevel());
        node.mutable_children()->CopyFrom(GetChildrenProto(child.second->GetChildren()));
        schedulerNodes.Add(std::move(node));
    }
    return schedulerNodes;
}

void SchedTree::AddChildFromProto(const Node::TreeNode &node, const messages::SchedulerNode &proto)
{
    for (const auto &child : proto.children()) {
        if (child.level() < 0) {
            YRLOG_ERROR("failed to add child {}-{} from proto, child level {} is less than zero", child.name(),
                        child.address(), child.level());
            continue;
        }
        auto level = static_cast<unsigned short>(child.level());
        YRLOG_INFO("add child node[name: {}, address: {}, level: {}] for parent node[name: {}]", child.name(),
                   child.address(), level, node->GetNodeInfo().name);
        auto childNode = std::make_shared<SchedNode>(NodeInfo{ child.name(), child.address() }, level);
        levelNodes_[level][child.name()] = childNode;
        node->AddChild(childNode);
        AddChildFromProto(childNode, child);
    }
}

std::unordered_map<std::string, Node::TreeNode> SchedTree::FindNodes(const uint64_t level)
{
    if (level >= levelNodes_.size()) {
        return {};
    }
    return levelNodes_.at(level);
}

}  // namespace functionsystem