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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_SECURITY_CONTEXT_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_SECURITY_CONTEXT_H_

#include <string>

#include "model_base.h"
#include "v1_capabilities.h"
#include "v1_seccomp_profile.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_SECURITY_CONTEXT = "V1SecurityContext";

class V1SecurityContext : public ModelBase {
public:
    V1SecurityContext();
    ~V1SecurityContext() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool IsAllowPrivilegeEscalation() const;
    bool AllowPrivilegeEscalationIsSet() const;
    void UnsetAllowPrivilegeEscalation();
    void SetAllowPrivilegeEscalation(bool value);

    std::shared_ptr<V1Capabilities> GetCapabilities() const;
    bool CapabilitiesIsSet() const;
    void UnsetCapabilities();
    void SetCapabilities(const std::shared_ptr<V1Capabilities> &value);

    bool IsPrivileged() const;
    bool PrivilegedIsSet() const;
    void UnsetPrivileged();
    void SetPrivileged(bool value);

    int64_t GetRunAsGroup() const;
    bool RunAsGroupIsSet() const;
    void UnsetRunAsGroup();
    void SetRunAsGroup(int64_t value);

    bool IsRunAsNonRoot() const;
    bool RunAsNonRootIsSet() const;
    void UnsetRunAsNonRoot();
    void SetRunAsNonRoot(bool value);

    int64_t GetRunAsUser() const;
    bool RunAsUserIsSet() const;
    void UnsetRunAsUser();
    void SetRunAsUser(int64_t value);

    std::shared_ptr<V1SeccompProfile> GetSeccompProfile() const;
    bool SeccompProfileIsSet() const;
    void UnSetSeccompProfile();
    void SetSeccompProfile(const std::shared_ptr<V1SeccompProfile>& value);

protected:
    bool m_allowPrivilegeEscalation;
    bool m_allowPrivilegeEscalationIsSet;
    std::shared_ptr<V1Capabilities> m_capabilities;
    bool m_capabilitiesIsSet;
    bool m_privileged;
    bool m_privilegedIsSet;
    int64_t m_runAsGroup;
    bool m_runAsGroupIsSet;
    bool m_runAsNonRoot;
    bool m_runAsNonRootIsSet;
    int64_t m_runAsUser;
    bool m_runAsUserIsSet;
    std::shared_ptr<V1SeccompProfile> m_seccompProfile;
    bool m_seccompProfileIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_SECURITY_CONTEXT_H_ */
