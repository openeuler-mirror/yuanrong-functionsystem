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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_NODE_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_NODE_H

#include <memory>
#include <string>
#include <unordered_map>

#include "common/proto/pb/message_pb.h"

namespace functionsystem {

enum class NodeState : char { CONNECTED = 0, BROKEN };

struct NodeInfo {
    std::string name;
    std::string address;
};

class Node {
public:
    using TreeNode = std::shared_ptr<Node>;
    using ChildNodes = std::unordered_map<std::string, TreeNode>;
    using ParentNode = std::weak_ptr<Node>;
    Node() = default;
    virtual ~Node() = default;

    virtual void SetState(const NodeState &state) = 0;
    virtual const NodeState &GetState() = 0;
    virtual const NodeInfo &GetNodeInfo() const = 0;
    virtual Node::TreeNode GetParent() const = 0;
    virtual const ChildNodes &GetChildren() const = 0;
    virtual messages::ScheduleTopology GetTopologyView() const = 0;
    virtual bool IsLeaf() const = 0;
    virtual void AddChild(const TreeNode &node) = 0;
    virtual void SetParent(const TreeNode &node) = 0;
    virtual void RemoveChild(const std::string &name) = 0;
    virtual void SetNodeInfo(const NodeInfo &nodeInfo) = 0;
    /**
     * Check whether a non-leaf node can be added to a parent node.
     * @param maxChildrenNum The max children number of the parent node.
     * @return True if the parent node level is greater than 1, and the number of child nodes is less than maxChildNum.
     */
    virtual bool CheckAddNonLeafNode(size_t maxChildrenNum) = 0;
    /**
     * Check whether a leaf node can be added to a parent node.
     * @param maxChildrenNum The max children number of the parent node.
     * @return  True if the parent node level is equal to 1, and the number of child nodes is less than maxChildNum.
     */
    virtual bool CheckAddLeafNode(size_t maxChildrenNum) = 0;
    virtual int GetLevel() = 0;
};

}  // namespace functionsystem
#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_NODE_H
