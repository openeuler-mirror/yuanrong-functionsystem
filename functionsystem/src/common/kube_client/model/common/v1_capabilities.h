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
 * V1Capabilities.h
 *
 * Adds and removes POSIX capabilities from running containers.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CAPABILITIES_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CAPABILITIES_H_

#include <string>
#include <vector>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_CAPABILITIES = "V1Capabilities";

class V1Capabilities : public ModelBase {
public:
    V1Capabilities();
    ~V1Capabilities() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool AddIsSet() const;
    void UnsetAdd();
    void SetAdd(const std::vector<std::string> &value);

    bool DropIsSet() const;
    void UnsetDrop();
    void SetDrop(const std::vector<std::string> &value);

protected:
    std::vector<std::string> m_add;
    bool m_addIsSet;
    std::vector<std::string> m_drop;
    bool m_dropIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1Capabilities_H_ */
