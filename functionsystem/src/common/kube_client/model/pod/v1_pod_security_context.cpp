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

#include "v1_pod_security_context.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodSecurityContext);

V1PodSecurityContext::V1PodSecurityContext()
{
    m_fsGroup = 0L;
    m_fsGroupIsSet = false;
    m_fsGroupChangePolicy = std::string("");
    m_fsGroupChangePolicyIsSet = false;
    m_runAsGroup = 0L;
    m_runAsGroupIsSet = false;
    m_runAsNonRoot = false;
    m_runAsNonRootIsSet = false;
    m_runAsUser = 0L;
    m_runAsUserIsSet = false;
    m_seccompProfileIsSet = false;
    m_supplementalGroupsIsSet = false;
}

V1PodSecurityContext::~V1PodSecurityContext()
{
}

nlohmann::json V1PodSecurityContext::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_fsGroupIsSet) {
        val["fsGroup"] = ModelUtils::ToJson(m_fsGroup);
    }
    if (m_fsGroupChangePolicyIsSet) {
        val["fsGroupChangePolicy"] = ModelUtils::ToJson(m_fsGroupChangePolicy);
    }
    if (m_runAsGroupIsSet) {
        val["runAsGroup"] = ModelUtils::ToJson(m_runAsGroup);
    }
    if (m_runAsNonRootIsSet) {
        val["runAsNonRoot"] = ModelUtils::ToJson(m_runAsNonRoot);
    }
    if (m_runAsUserIsSet) {
        val["runAsUser"] = ModelUtils::ToJson(m_runAsUser);
    }
    if (m_seccompProfileIsSet) {
        val["seccompProfile"] = ModelUtils::ToJson(m_seccompProfile);
    }
    if (m_supplementalGroupsIsSet) {
        val["supplementalGroups"] = ModelUtils::ToJson(m_supplementalGroups);
    }
    return val;
}

bool V1PodSecurityContext::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("fsGroup")) {
        const nlohmann::json &fieldValue = val.at("fsGroup");
        if (!fieldValue.is_null()) {
            int64_t refValSetFsGroup;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFsGroup);
            SetFsGroup(refValSetFsGroup);
        }
    }
    if (val.contains("fsGroupChangePolicy")) {
        const nlohmann::json &fieldValue = val.at("fsGroupChangePolicy");
        if (!fieldValue.is_null()) {
            std::string refValSetFsGroupChangePolicy;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFsGroupChangePolicy);
            SetFsGroupChangePolicy(refValSetFsGroupChangePolicy);
        }
    }
    if (ModelUtils::CheckHasKey(val, "runAsGroup")) {
        const nlohmann::json &fieldValue = val.at("runAsGroup");
        int64_t refValSetRunAsGroup;
        ok &= ModelUtils::FromJson(fieldValue, refValSetRunAsGroup);
        SetRunAsGroup(refValSetRunAsGroup);
    }
    if (ModelUtils::CheckHasKey(val, "runAsNonRoot")) {
        const nlohmann::json &fieldValue = val.at("runAsNonRoot");
        bool refValSetRunAsNonRoot;
        ok &= ModelUtils::FromJson(fieldValue, refValSetRunAsNonRoot);
        SetRunAsNonRoot(refValSetRunAsNonRoot);
    }
    if (ModelUtils::CheckHasKey(val, "runAsUser")) {
        const nlohmann::json &fieldValue = val.at("runAsUser");
        int64_t refValSetRunAsUser;
        ok &= ModelUtils::FromJson(fieldValue, refValSetRunAsUser);
        SetRunAsUser(refValSetRunAsUser);
    }
    if (ModelUtils::CheckHasKey(val, "seccompProfile")) {
        std::shared_ptr<V1SeccompProfile> refValSetSeccompProfile;
        ok &= ModelUtils::FromJson(val.at("seccompProfile"), refValSetSeccompProfile);
        SetSeccompProfile(refValSetSeccompProfile);
    }
    if (ModelUtils::CheckHasKey(val, "supplementalGroups")) {
        std::vector<int64_t> refValSetSupplementalGroups;
        ok &= ModelUtils::FromJson(val.at("supplementalGroups"), refValSetSupplementalGroups);
        SetSupplementalGroups(refValSetSupplementalGroups);
    }
    return ok;
}

int64_t V1PodSecurityContext::GetFsGroup() const
{
    return m_fsGroup;
}

void V1PodSecurityContext::SetFsGroup(int64_t value)
{
    m_fsGroup = value;
    m_fsGroupIsSet = true;
}

bool V1PodSecurityContext::FsGroupIsSet() const
{
    return m_fsGroupIsSet;
}

void V1PodSecurityContext::UnsetFsGroup()
{
    m_fsGroupIsSet = false;
}
std::string V1PodSecurityContext::GetFsGroupChangePolicy() const
{
    return m_fsGroupChangePolicy;
}

void V1PodSecurityContext::SetFsGroupChangePolicy(const std::string &value)
{
    m_fsGroupChangePolicy = value;
    m_fsGroupChangePolicyIsSet = true;
}

bool V1PodSecurityContext::FsGroupChangePolicyIsSet() const
{
    return m_fsGroupChangePolicyIsSet;
}

void V1PodSecurityContext::UnsetFsGroupChangePolicy()
{
    m_fsGroupChangePolicyIsSet = false;
}
int64_t V1PodSecurityContext::GetRunAsGroup() const
{
    return m_runAsGroup;
}

void V1PodSecurityContext::SetRunAsGroup(int64_t value)
{
    m_runAsGroup = value;
    m_runAsGroupIsSet = true;
}

bool V1PodSecurityContext::RunAsGroupIsSet() const
{
    return m_runAsGroupIsSet;
}

void V1PodSecurityContext::UnsetRunAsGroup()
{
    m_runAsGroupIsSet = false;
}
bool V1PodSecurityContext::IsRunAsNonRoot() const
{
    return m_runAsNonRoot;
}

void V1PodSecurityContext::SetRunAsNonRoot(bool value)
{
    m_runAsNonRoot = value;
    m_runAsNonRootIsSet = true;
}

bool V1PodSecurityContext::RunAsNonRootIsSet() const
{
    return m_runAsNonRootIsSet;
}

void V1PodSecurityContext::UnsetRunAsNonRoot()
{
    m_runAsNonRootIsSet = false;
}
int64_t V1PodSecurityContext::GetRunAsUser() const
{
    return m_runAsUser;
}

void V1PodSecurityContext::SetRunAsUser(int64_t value)
{
    m_runAsUser = value;
    m_runAsUserIsSet = true;
}

bool V1PodSecurityContext::RunAsUserIsSet() const
{
    return m_runAsUserIsSet;
}

void V1PodSecurityContext::UnsetRunAsUser()
{
    m_runAsUserIsSet = false;
}
std::shared_ptr<V1SeccompProfile> V1PodSecurityContext::GetSeccompProfile() const
{
    return m_seccompProfile;
}

void V1PodSecurityContext::SetSeccompProfile(const std::shared_ptr<V1SeccompProfile>& value)
{
    m_seccompProfile = value;
    m_seccompProfileIsSet = true;
}

bool V1PodSecurityContext::SeccompProfileIsSet() const
{
    return m_seccompProfileIsSet;
}

void V1PodSecurityContext::UnSetSeccompProfile()
{
    m_seccompProfileIsSet = false;
}

std::vector<int64_t> &V1PodSecurityContext::GetSupplementalGroups()
{
    return m_supplementalGroups;
}

void V1PodSecurityContext::SetSupplementalGroups(std::vector<int64_t> value)
{
    m_supplementalGroups = value;
    m_supplementalGroupsIsSet = true;
}

bool V1PodSecurityContext::SupplementalGroupsIsSet() const
{
    return m_supplementalGroupsIsSet;
}

void V1PodSecurityContext::UnsetSupplementalGroups()
{
    m_supplementalGroupsIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
