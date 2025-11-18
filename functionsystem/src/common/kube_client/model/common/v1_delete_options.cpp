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

#include "v1_delete_options.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1DeleteOptions);

V1DeleteOptions::V1DeleteOptions()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_dryRunIsSet = false;
    m_gracePeriodSeconds = 0L;
    m_gracePeriodSecondsIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_orphanDependents = false;
    m_orphanDependentsIsSet = false;
    m_propagationPolicy = std::string("");
    m_propagationPolicyIsSet = false;
}

V1DeleteOptions::~V1DeleteOptions()
{
}

nlohmann::json V1DeleteOptions::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_dryRunIsSet) {
        val["dryRun"] = ModelUtils::ToJson(m_dryRun);
    }
    if (m_gracePeriodSecondsIsSet) {
        val["gracePeriodSeconds"] = ModelUtils::ToJson(m_gracePeriodSeconds);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    if (m_orphanDependentsIsSet) {
        val["orphanDependents"] = ModelUtils::ToJson(m_orphanDependents);
    }
    if (m_propagationPolicyIsSet) {
        val["propagationPolicy"] = ModelUtils::ToJson(m_propagationPolicy);
    }

    return val;
}

bool V1DeleteOptions::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains("dryRun")) {
        const nlohmann::json &fieldValue = val.at("dryRun");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetDryRun;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDryRun);
            SetDryRun(refValSetDryRun);
        }
    }
    if (val.contains("gracePeriodSeconds")) {
        const nlohmann::json &fieldValue = val.at("gracePeriodSeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetGracePeriodSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetGracePeriodSeconds);
            SetGracePeriodSeconds(refValSetGracePeriodSeconds);
        }
    }
    if (val.contains("kind")) {
        const nlohmann::json &fieldValue = val.at("kind");
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    if (val.contains("orphanDependents")) {
        const nlohmann::json &fieldValue = val.at("orphanDependents");
        if (!fieldValue.is_null()) {
            bool refValSetOrphanDependents;
            ok &= ModelUtils::FromJson(fieldValue, refValSetOrphanDependents);
            SetOrphanDependents(refValSetOrphanDependents);
        }
    }
    if (val.contains("propagationPolicy")) {
        const nlohmann::json &fieldValue = val.at("propagationPolicy");
        if (!fieldValue.is_null()) {
            std::string refValSetPropagationPolicy;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPropagationPolicy);
            SetPropagationPolicy(refValSetPropagationPolicy);
        }
    }
    return ok;
}

std::string V1DeleteOptions::GetApiVersion() const
{
    return m_apiVersion;
}

void V1DeleteOptions::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1DeleteOptions::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1DeleteOptions::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::vector<std::string> &V1DeleteOptions::GetDryRun()
{
    return m_dryRun;
}

void V1DeleteOptions::SetDryRun(const std::vector<std::string> &value)
{
    m_dryRun = value;
    m_dryRunIsSet = true;
}

bool V1DeleteOptions::DryRunIsSet() const
{
    return m_dryRunIsSet;
}

void V1DeleteOptions::UnsetDryRun()
{
    m_dryRunIsSet = false;
}
int64_t V1DeleteOptions::GetGracePeriodSeconds() const
{
    return m_gracePeriodSeconds;
}

void V1DeleteOptions::SetGracePeriodSeconds(int64_t value)
{
    m_gracePeriodSeconds = value;
    m_gracePeriodSecondsIsSet = true;
}

bool V1DeleteOptions::GracePeriodSecondsIsSet() const
{
    return m_gracePeriodSecondsIsSet;
}

void V1DeleteOptions::UnsetGracePeriodSeconds()
{
    m_gracePeriodSecondsIsSet = false;
}
std::string V1DeleteOptions::GetKind() const
{
    return m_kind;
}

void V1DeleteOptions::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1DeleteOptions::KindIsSet() const
{
    return m_kindIsSet;
}

void V1DeleteOptions::UnsetKind()
{
    m_kindIsSet = false;
}
bool V1DeleteOptions::IsOrphanDependents() const
{
    return m_orphanDependents;
}

void V1DeleteOptions::SetOrphanDependents(bool value)
{
    m_orphanDependents = value;
    m_orphanDependentsIsSet = true;
}

bool V1DeleteOptions::OrphanDependentsIsSet() const
{
    return m_orphanDependentsIsSet;
}

void V1DeleteOptions::UnsetOrphanDependents()
{
    m_orphanDependentsIsSet = false;
}

std::string V1DeleteOptions::GetPropagationPolicy() const
{
    return m_propagationPolicy;
}

void V1DeleteOptions::SetPropagationPolicy(const std::string &value)
{
    m_propagationPolicy = value;
    m_propagationPolicyIsSet = true;
}

bool V1DeleteOptions::PropagationPolicyIsSet() const
{
    return m_propagationPolicyIsSet;
}

void V1DeleteOptions::UnsetPropagationPolicy()
{
    m_propagationPolicyIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
