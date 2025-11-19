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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LEASE_SPEC_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LEASE_SPEC_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

class V1LeaseSpec : public ModelBase {
public:
    V1LeaseSpec();
    ~V1LeaseSpec() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& json) override;

    utility::Datetime GetAcquireTime() const;
    bool AcquireTimeIsSet() const;
    void UnsetAcquireTime();
    void SetAcquireTime(const utility::Datetime& value);

    std::string GetHolderIdentity() const;
    bool HolderIdentityIsSet() const;
    void UnsetHolderIdentity();
    void SetHolderIdentity(const std::string& value);

    int32_t GetLeaseDurationSeconds() const;
    bool LeaseDurationSecondsIsSet() const;
    void UnsetLeaseDurationSeconds();
    void SetLeaseDurationSeconds(int32_t value);

    int32_t GetLeaseTransitions() const;
    bool LeaseTransitionsIsSet() const;
    void UnsetLeaseTransitions();
    void SetLeaseTransitions(int32_t value);

    utility::Datetime GetRenewTime() const;
    bool RenewTimeIsSet() const;
    void UnsetRenewTime();
    void SetRenewTime(const utility::Datetime& value);
protected:
    utility::Datetime m_acquireTime;
    bool m_acquireTimeIsSet;
    std::string m_holderIdentity;
    bool m_holderIdentityIsSet;
    int32_t m_leaseDurationSeconds;
    bool m_leaseDurationSecondsIsSet;
    int32_t m_leaseTransitions;
    bool m_leaseTransitionsIsSet;
    utility::Datetime m_renewTime;
    bool m_renewTimeIsSet;
};
}
}
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LEASE_SPEC_H_ */
