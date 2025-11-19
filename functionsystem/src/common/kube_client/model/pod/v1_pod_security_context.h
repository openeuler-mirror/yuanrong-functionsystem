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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_SECURITY_CONTEXT_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_SECURITY_CONTEXT_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_seccomp_profile.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_POD_SECURITY_CONTEXT = "V1PodSecurityContext";

class V1PodSecurityContext : public ModelBase {
public:
    V1PodSecurityContext();
    ~V1PodSecurityContext() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    int64_t GetFsGroup() const;
    bool FsGroupIsSet() const;
    void UnsetFsGroup();
    void SetFsGroup(int64_t value);

    std::string GetFsGroupChangePolicy() const;
    bool FsGroupChangePolicyIsSet() const;
    void UnsetFsGroupChangePolicy();
    void SetFsGroupChangePolicy(const std::string &value);

    bool RunAsGroupIsSet() const;
    void UnsetRunAsGroup();
    int64_t GetRunAsGroup() const;
    void SetRunAsGroup(int64_t value);

    bool IsRunAsNonRoot() const;
    void SetRunAsNonRoot(bool value);
    bool RunAsNonRootIsSet() const;
    void UnsetRunAsNonRoot();

    int64_t GetRunAsUser() const;
    bool RunAsUserIsSet() const;
    void UnsetRunAsUser();
    void SetRunAsUser(int64_t value);

    std::shared_ptr<V1SeccompProfile> GetSeccompProfile() const;
    bool SeccompProfileIsSet() const;
    void UnSetSeccompProfile();
    void SetSeccompProfile(const std::shared_ptr<V1SeccompProfile>& value);

    std::vector<int64_t>& GetSupplementalGroups();
    bool SupplementalGroupsIsSet() const;
    void UnsetSupplementalGroups();
    void SetSupplementalGroups(std::vector<int64_t> value);

protected:
    bool m_runAsNonRoot;
    bool m_runAsNonRootIsSet;
    int64_t m_runAsUser;
    bool m_runAsUserIsSet;
    int64_t m_fsGroup;
    bool m_fsGroupIsSet;
    std::string m_fsGroupChangePolicy;
    bool m_fsGroupChangePolicyIsSet;
    int64_t m_runAsGroup;
    bool m_runAsGroupIsSet;
    std::shared_ptr<V1SeccompProfile> m_seccompProfile;
    bool m_seccompProfileIsSet;
    std::vector<int64_t> m_supplementalGroups;
    bool m_supplementalGroupsIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_SECURITY_CONTEXT_H_ */
