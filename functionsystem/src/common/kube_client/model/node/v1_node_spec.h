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
 * V1NodeSpec.h
 *
 * NodeSpec describes the attributes that a node is created with.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_SPEC_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_SPEC_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "v1_taint.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE_SPEC = "V1NodeSpec";

class V1NodeSpec : public ModelBase {
public:
    V1NodeSpec();
    ~V1NodeSpec() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetExternalID() const;
    bool ExternalIDIsSet() const;
    void UnsetExternalID();
    void SetExternalID(const std::string &value);

    std::string GetPodCIDR() const;
    bool PodCIDRIsSet() const;
    void UnSetPodCIDR();
    void SetPodCIDR(const std::string &value);

    std::vector<std::string> &GetPodCIDRs();
    bool PodCIDRsIsSet() const;
    void UnSetPodCIDRs();
    void SetPodCIDRs(const std::vector<std::string> &value);

    std::string GetProviderID() const;
    bool ProviderIDIsSet() const;
    void UnsetProviderID();
    void SetProviderID(const std::string &value);

    std::vector<std::shared_ptr<V1Taint>> &GetTaints();
    bool TaintsIsSet() const;
    void UnsetTaints();
    void SetTaints(const std::vector<std::shared_ptr<V1Taint>> &value);

    bool IsUnschedulable() const;
    bool UnschedulableIsSet() const;
    void UnsetUnschedulable();
    void SetUnschedulable(bool value);

protected:
    std::string m_externalID;
    bool m_externalIDIsSet;
    std::string m_podCIDR;
    bool m_podCIDRIsSet;
    std::vector<std::string> m_podCIDRs;
    bool m_podCIDRsIsSet;
    std::string m_providerID;
    bool m_providerIDIsSet;
    std::vector<std::shared_ptr<V1Taint>> m_taints;
    bool m_taintsIsSet;
    bool m_unschedulable;
    bool m_unschedulableIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_SPEC_H_ */
