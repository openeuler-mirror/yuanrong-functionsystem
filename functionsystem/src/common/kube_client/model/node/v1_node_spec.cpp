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

#include "v1_node_spec.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1NodeSpec);

V1NodeSpec::V1NodeSpec()
{
    m_externalID = std::string("");
    m_externalIDIsSet = false;
    m_podCIDR = std::string("");
    m_podCIDRIsSet = false;
    m_podCIDRsIsSet = false;
    m_providerID = std::string("");
    m_providerIDIsSet = false;
    m_taintsIsSet = false;
    m_unschedulable = false;
    m_unschedulableIsSet = false;
}

V1NodeSpec::~V1NodeSpec()
{
}

nlohmann::json V1NodeSpec::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_externalIDIsSet) {
        val["externalID"] = ModelUtils::ToJson(m_externalID);
    }
    if (m_podCIDRIsSet) {
        val["podCIDR"] = ModelUtils::ToJson(m_podCIDR);
    }
    if (m_podCIDRsIsSet) {
        val["podCIDRs"] = ModelUtils::ToJson(m_podCIDRs);
    }
    if (m_providerIDIsSet) {
        val["providerID"] = ModelUtils::ToJson(m_providerID);
    }
    if (m_taintsIsSet) {
        val["taints"] = ModelUtils::ToJson(m_taints);
    }
    if (m_unschedulableIsSet) {
        val["unschedulable"] = ModelUtils::ToJson(m_unschedulable);
    }

    return val;
}

bool V1NodeSpec::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("externalID")) {
        const nlohmann::json &fieldValue = val.at("externalID");
        if (!fieldValue.is_null()) {
            std::string refValSetExternalID;
            ok &= ModelUtils::FromJson(fieldValue, refValSetExternalID);
            SetExternalID(refValSetExternalID);
        }
    }
    if (val.contains("podCIDR")) {
        const nlohmann::json &fieldValue = val.at("podCIDR");
        if (!fieldValue.is_null()) {
            std::string refValSetPodCIDR;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPodCIDR);
            SetPodCIDR(refValSetPodCIDR);
        }
    }
    if (val.contains("podCIDRs")) {
        const nlohmann::json &fieldValue = val.at("podCIDRs");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetPodCIDRs;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPodCIDRs);
            SetPodCIDRs(refValSetPodCIDRs);
        }
    }
    if (val.contains("providerID")) {
        const nlohmann::json &fieldValue = val.at("providerID");
        if (!fieldValue.is_null()) {
            std::string refValSetProviderID;
            ok &= ModelUtils::FromJson(fieldValue, refValSetProviderID);
            SetProviderID(refValSetProviderID);
        }
    }
    if (val.contains("taints")) {
        const nlohmann::json &fieldValue = val.at("taints");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1Taint>> refValSetTaints;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTaints);
            SetTaints(refValSetTaints);
        }
    }
    if (val.contains("unschedulable")) {
        const nlohmann::json &fieldValue = val.at("unschedulable");
        if (!fieldValue.is_null()) {
            bool refValSetUnschedulable;
            ok &= ModelUtils::FromJson(fieldValue, refValSetUnschedulable);
            SetUnschedulable(refValSetUnschedulable);
        }
    }
    return ok;
}

std::string V1NodeSpec::GetExternalID() const
{
    return m_externalID;
}

void V1NodeSpec::SetExternalID(const std::string &value)
{
    m_externalID = value;
    m_externalIDIsSet = true;
}

bool V1NodeSpec::ExternalIDIsSet() const
{
    return m_externalIDIsSet;
}

void V1NodeSpec::UnsetExternalID()
{
    m_externalIDIsSet = false;
}
std::string V1NodeSpec::GetPodCIDR() const
{
    return m_podCIDR;
}

void V1NodeSpec::SetPodCIDR(const std::string &value)
{
    m_podCIDR = value;
    m_podCIDRIsSet = true;
}

bool V1NodeSpec::PodCIDRIsSet() const
{
    return m_podCIDRIsSet;
}

void V1NodeSpec::UnSetPodCIDR()
{
    m_podCIDRIsSet = false;
}

std::vector<std::string> &V1NodeSpec::GetPodCIDRs()
{
    return m_podCIDRs;
}

void V1NodeSpec::SetPodCIDRs(const std::vector<std::string> &value)
{
    m_podCIDRs = value;
    m_podCIDRsIsSet = true;
}

bool V1NodeSpec::PodCIDRsIsSet() const
{
    return m_podCIDRsIsSet;
}

void V1NodeSpec::UnSetPodCIDRs()
{
    m_podCIDRsIsSet = false;
}
std::string V1NodeSpec::GetProviderID() const
{
    return m_providerID;
}

void V1NodeSpec::SetProviderID(const std::string &value)
{
    m_providerID = value;
    m_providerIDIsSet = true;
}

bool V1NodeSpec::ProviderIDIsSet() const
{
    return m_providerIDIsSet;
}

void V1NodeSpec::UnsetProviderID()
{
    m_providerIDIsSet = false;
}

std::vector<std::shared_ptr<V1Taint>> &V1NodeSpec::GetTaints()
{
    return m_taints;
}

void V1NodeSpec::SetTaints(const std::vector<std::shared_ptr<V1Taint>> &value)
{
    m_taints = value;
    m_taintsIsSet = true;
}

bool V1NodeSpec::TaintsIsSet() const
{
    return m_taintsIsSet;
}

void V1NodeSpec::UnsetTaints()
{
    m_taintsIsSet = false;
}
bool V1NodeSpec::IsUnschedulable() const
{
    return m_unschedulable;
}

void V1NodeSpec::SetUnschedulable(bool value)
{
    m_unschedulable = value;
    m_unschedulableIsSet = true;
}

bool V1NodeSpec::UnschedulableIsSet() const
{
    return m_unschedulableIsSet;
}

void V1NodeSpec::UnsetUnschedulable()
{
    m_unschedulableIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
