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

/*
 * V1NodeStatus.h
 *
 * NodeStatus is information about the current status of a node.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_STATUS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_STATUS_H_

#include <map>
#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "v1_node_address.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE_STATUS = "V1NodeStatus";

class V1NodeStatus : public ModelBase {
public:
    V1NodeStatus();
    ~V1NodeStatus() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::vector<std::shared_ptr<V1NodeAddress>> &GetAddresses();
    bool AddressesIsSet() const;
    void UnsetAddresses();
    void SetAddresses(const std::vector<std::shared_ptr<V1NodeAddress>> &value);

protected:
    std::vector<std::shared_ptr<V1NodeAddress>> m_addresses;
    bool m_addressesIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_STATUS_H_ */
