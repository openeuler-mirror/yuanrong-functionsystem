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

#include "v1_resource_field_selector.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ResourceFieldSelector);

V1ResourceFieldSelector::V1ResourceFieldSelector()
{
    m_containerName = std::string("");
    m_containerNameIsSet = false;
    m_divisor = std::string("");
    m_divisorIsSet = false;
    m_resource = std::string("");
    m_resourceIsSet = false;
}

V1ResourceFieldSelector::~V1ResourceFieldSelector()
{
}

nlohmann::json V1ResourceFieldSelector::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerNameIsSet) {
        val["containerName"] = ModelUtils::ToJson(m_containerName);
    }
    if (m_divisorIsSet) {
        val["divisor"] = ModelUtils::ToJson(m_divisor);
    }
    if (m_resourceIsSet) {
        val["resource"] = ModelUtils::ToJson(m_resource);
    }

    return val;
}

bool V1ResourceFieldSelector::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("containerName")) {
        const nlohmann::json &fieldValue = val.at("containerName");
        if (!fieldValue.is_null()) {
            std::string refValSetContainerName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetContainerName);
            SetContainerName(refValSetContainerName);
        }
    }
    if (val.contains("divisor")) {
        const nlohmann::json &fieldValue = val.at("divisor");
        if (!fieldValue.is_null()) {
            std::string refValSetDivisor;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDivisor);
            SetDivisor(refValSetDivisor);
        }
    }
    if (val.contains("resource")) {
        const nlohmann::json &fieldValue = val.at("resource");
        if (!fieldValue.is_null()) {
            std::string refValSetResource;
            ok &= ModelUtils::FromJson(fieldValue, refValSetResource);
            SetResource(refValSetResource);
        }
    }
    return ok;
}

std::string V1ResourceFieldSelector::GetContainerName() const
{
    return m_containerName;
}

void V1ResourceFieldSelector::SetContainerName(const std::string &value)
{
    m_containerName = value;
    m_containerNameIsSet = true;
}

bool V1ResourceFieldSelector::ContainerNameIsSet() const
{
    return m_containerNameIsSet;
}

void V1ResourceFieldSelector::UnsetContainerName()
{
    m_containerNameIsSet = false;
}
std::string V1ResourceFieldSelector::GetDivisor() const
{
    return m_divisor;
}

void V1ResourceFieldSelector::SetDivisor(const std::string &value)
{
    m_divisor = value;
    m_divisorIsSet = true;
}

bool V1ResourceFieldSelector::DivisorIsSet() const
{
    return m_divisorIsSet;
}

void V1ResourceFieldSelector::UnsetDivisor()
{
    m_divisorIsSet = false;
}
std::string V1ResourceFieldSelector::GetResource() const
{
    return m_resource;
}

void V1ResourceFieldSelector::SetResource(const std::string &value)
{
    m_resource = value;
    m_resourceIsSet = true;
}

bool V1ResourceFieldSelector::ResourceIsSet() const
{
    return m_resourceIsSet;
}

void V1ResourceFieldSelector::UnsetResource()
{
    m_resourceIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
