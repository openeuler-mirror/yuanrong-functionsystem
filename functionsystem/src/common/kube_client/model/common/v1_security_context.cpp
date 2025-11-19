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

#include "v1_security_context.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1SecurityContext);

V1SecurityContext::V1SecurityContext()
{
    m_allowPrivilegeEscalation = false;
    m_allowPrivilegeEscalationIsSet = false;
    m_capabilitiesIsSet = false;
    m_privileged = false;
    m_privilegedIsSet = false;
    m_runAsGroup = 0L;
    m_runAsGroupIsSet = false;
    m_runAsNonRoot = false;
    m_runAsNonRootIsSet = false;
    m_runAsUser = 0L;
    m_runAsUserIsSet = false;
    m_seccompProfileIsSet = false;
}

V1SecurityContext::~V1SecurityContext()
{
}

nlohmann::json V1SecurityContext::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_runAsNonRootIsSet) {
        val["runAsNonRoot"] = ModelUtils::ToJson(m_runAsNonRoot);
    }
    if (m_runAsUserIsSet) {
        val["runAsUser"] = ModelUtils::ToJson(m_runAsUser);
    }
    if (m_allowPrivilegeEscalationIsSet) {
        val["allowPrivilegeEscalation"] = ModelUtils::ToJson(m_allowPrivilegeEscalation);
    }
    if (m_capabilitiesIsSet) {
        val["capabilities"] = ModelUtils::ToJson(m_capabilities);
    }
    if (m_privilegedIsSet) {
        val["privileged"] = ModelUtils::ToJson(m_privileged);
    }
    if (m_runAsGroupIsSet) {
        val["runAsGroup"] = ModelUtils::ToJson(m_runAsGroup);
    }
    if (m_seccompProfileIsSet) {
        val["seccompProfile"] = ModelUtils::ToJson(m_seccompProfile);
    }
    return val;
}

bool V1SecurityContext::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "allowPrivilegeEscalation")) {
        bool refValSetAllowPrivilegeEscalation;
        ok &= ModelUtils::FromJson(val.at("allowPrivilegeEscalation"), refValSetAllowPrivilegeEscalation);
        SetAllowPrivilegeEscalation(refValSetAllowPrivilegeEscalation);
    }
    if (val.contains("capabilities")) {
        const nlohmann::json &fieldValue = val.at("capabilities");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1Capabilities> refValSetCapabilities;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCapabilities);
            SetCapabilities(refValSetCapabilities);
        }
    }
    if (val.contains("privileged")) {
        const nlohmann::json &fieldValue = val.at("privileged");
        if (!fieldValue.is_null()) {
            bool refValSetPrivileged;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPrivileged);
            SetPrivileged(refValSetPrivileged);
        }
    }
    if (val.contains("runAsGroup")) {
        const nlohmann::json &fieldValue = val.at("runAsGroup");
        if (!fieldValue.is_null()) {
            int64_t refValSetRunAsGroup;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRunAsGroup);
            SetRunAsGroup(refValSetRunAsGroup);
        }
    }
    if (ModelUtils::CheckHasKey(val, "runAsNonRoot")) {
        bool refValSetRunAsNonRoot;
        ok &= ModelUtils::FromJson(val.at("runAsNonRoot"), refValSetRunAsNonRoot);
        SetRunAsNonRoot(refValSetRunAsNonRoot);
    }
    if (val.contains("runAsUser")) {
        const nlohmann::json &fieldValue = val.at("runAsUser");
        if (!fieldValue.is_null()) {
            int64_t refValSetRunAsUser;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRunAsUser);
            SetRunAsUser(refValSetRunAsUser);
        }
    }
    if (ModelUtils::CheckHasKey(val, "seccompProfile")) {
        std::shared_ptr<V1SeccompProfile> refValSetSeccompProfile;
        ok &= ModelUtils::FromJson(val.at("seccompProfile"), refValSetSeccompProfile);
        SetSeccompProfile(refValSetSeccompProfile);
    }
    return ok;
}

bool V1SecurityContext::IsAllowPrivilegeEscalation() const
{
    return m_allowPrivilegeEscalation;
}

void V1SecurityContext::SetAllowPrivilegeEscalation(bool value)
{
    m_allowPrivilegeEscalation = value;
    m_allowPrivilegeEscalationIsSet = true;
}

bool V1SecurityContext::AllowPrivilegeEscalationIsSet() const
{
    return m_allowPrivilegeEscalationIsSet;
}

void V1SecurityContext::UnsetAllowPrivilegeEscalation()
{
    m_allowPrivilegeEscalationIsSet = false;
}
std::shared_ptr<V1Capabilities> V1SecurityContext::GetCapabilities() const
{
    return m_capabilities;
}

void V1SecurityContext::SetCapabilities(const std::shared_ptr<V1Capabilities> &value)
{
    m_capabilities = value;
    m_capabilitiesIsSet = true;
}

bool V1SecurityContext::CapabilitiesIsSet() const
{
    return m_capabilitiesIsSet;
}

void V1SecurityContext::UnsetCapabilities()
{
    m_capabilitiesIsSet = false;
}
bool V1SecurityContext::IsPrivileged() const
{
    return m_privileged;
}

void V1SecurityContext::SetPrivileged(bool value)
{
    m_privileged = value;
    m_privilegedIsSet = true;
}

bool V1SecurityContext::PrivilegedIsSet() const
{
    return m_privilegedIsSet;
}

void V1SecurityContext::UnsetPrivileged()
{
    m_privilegedIsSet = false;
}
int64_t V1SecurityContext::GetRunAsGroup() const
{
    return m_runAsGroup;
}

void V1SecurityContext::SetRunAsGroup(int64_t value)
{
    m_runAsGroup = value;
    m_runAsGroupIsSet = true;
}

bool V1SecurityContext::RunAsGroupIsSet() const
{
    return m_runAsGroupIsSet;
}

void V1SecurityContext::UnsetRunAsGroup()
{
    m_runAsGroupIsSet = false;
}
bool V1SecurityContext::IsRunAsNonRoot() const
{
    return m_runAsNonRoot;
}

void V1SecurityContext::SetRunAsNonRoot(bool value)
{
    m_runAsNonRoot = value;
    m_runAsNonRootIsSet = true;
}

bool V1SecurityContext::RunAsNonRootIsSet() const
{
    return m_runAsNonRootIsSet;
}

void V1SecurityContext::UnsetRunAsNonRoot()
{
    m_runAsNonRootIsSet = false;
}
int64_t V1SecurityContext::GetRunAsUser() const
{
    return m_runAsUser;
}

void V1SecurityContext::SetRunAsUser(int64_t value)
{
    m_runAsUser = value;
    m_runAsUserIsSet = true;
}

bool V1SecurityContext::RunAsUserIsSet() const
{
    return m_runAsUserIsSet;
}

void V1SecurityContext::UnsetRunAsUser()
{
    m_runAsUserIsSet = false;
}
std::shared_ptr<V1SeccompProfile> V1SecurityContext::GetSeccompProfile() const
{
    return m_seccompProfile;
}

void V1SecurityContext::SetSeccompProfile(const std::shared_ptr<V1SeccompProfile> &value)
{
    m_seccompProfile = value;
    m_seccompProfileIsSet = true;
}

bool V1SecurityContext::SeccompProfileIsSet() const
{
    return m_seccompProfileIsSet;
}

void V1SecurityContext::UnSetSeccompProfile()
{
    m_seccompProfileIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
