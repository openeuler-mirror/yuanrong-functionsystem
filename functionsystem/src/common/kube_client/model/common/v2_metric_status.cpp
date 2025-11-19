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

#include "v2_metric_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2MetricStatus);

V2MetricStatus::V2MetricStatus() noexcept : m_containerResourceIsSet(false),
    m_externalIsSet(false),
    m_objectIsSet(false),
    m_podsIsSet(false),
    m_resourceIsSet(false),
    m_type(std::string("")),
    m_typeIsSet(false)
{}

V2MetricStatus::~V2MetricStatus()
{
}

nlohmann::json V2MetricStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerResourceIsSet) {
        val[std::string(("containerResource"))] = ModelUtils::ToJson(m_containerResource);
    }
    if (m_externalIsSet) {
        val[std::string(("external"))] = ModelUtils::ToJson(m_external);
    }
    if (m_objectIsSet) {
        val[std::string(("object"))] = ModelUtils::ToJson(m_object);
    }
    if (m_podsIsSet) {
        val[std::string(("pods"))] = ModelUtils::ToJson(m_pods);
    }
    if (m_resourceIsSet) {
        val[std::string(("resource"))] = ModelUtils::ToJson(m_resource);
    }
    if (m_typeIsSet) {
        val[std::string(("type"))] = ModelUtils::ToJson(m_type);
    }

    return val;
}

bool V2MetricStatus::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("containerResource")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("containerResource")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2ContainerResourceMetricStatus> refValSetContainerResource;
            ok &= ModelUtils::FromJson(fieldValue, refValSetContainerResource);
            SetContainerResource(refValSetContainerResource);
        }
    }
    if (val.contains(std::string(("external")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("external")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2ExternalMetricStatus> refValSetExternal;
            ok &= ModelUtils::FromJson(fieldValue, refValSetExternal);
            SetExternal(refValSetExternal);
        }
    }
    if (val.contains(std::string(("object")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("object")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2ObjectMetricStatus> refValSetObject;
            ok &= ModelUtils::FromJson(fieldValue, refValSetObject);
            SetObject(refValSetObject);
        }
    }
    if (val.contains(std::string(("pods")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("pods")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2PodsMetricStatus> refValSetPods;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPods);
            SetPods(refValSetPods);
        }
    }
    if (val.contains(std::string(("resource")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("resource")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2ResourceMetricStatus> refValSetResource;
            ok &= ModelUtils::FromJson(fieldValue, refValSetResource);
            SetResource(refValSetResource);
        }
    }
    if (val.contains(std::string("type"))) {
        const nlohmann::json& fieldValue = val.at(std::string("type"));
        if (!fieldValue.is_null()) {
            std::string refValSetType;
            ok &= ModelUtils::FromJson(fieldValue, refValSetType);
            SetType(refValSetType);
        }
    }
    return ok;
}

std::shared_ptr<V2ContainerResourceMetricStatus> V2MetricStatus::GetContainerResource() const
{
    return m_containerResource;
}

void V2MetricStatus::SetContainerResource(const std::shared_ptr<V2ContainerResourceMetricStatus>& value)
{
    m_containerResource = value;
    m_containerResourceIsSet = true;
}

bool V2MetricStatus::ContainerResourceIsSet() const
{
    return m_containerResourceIsSet;
}

void V2MetricStatus::UnsetContainerResource()
{
    m_containerResourceIsSet = false;
}
std::shared_ptr<V2ExternalMetricStatus> V2MetricStatus::GetExternal() const
{
    return m_external;
}

void V2MetricStatus::SetExternal(const std::shared_ptr<V2ExternalMetricStatus>& value)
{
    m_external = value;
    m_externalIsSet = true;
}

bool V2MetricStatus::ExternalIsSet() const
{
    return m_externalIsSet;
}

void V2MetricStatus::UnsetExternal()
{
    m_externalIsSet = false;
}
std::shared_ptr<V2ObjectMetricStatus> V2MetricStatus::GetObject() const
{
    return m_object;
}

void V2MetricStatus::SetObject(const std::shared_ptr<V2ObjectMetricStatus>& value)
{
    m_object = value;
    m_objectIsSet = true;
}

bool V2MetricStatus::ObjectIsSet() const
{
    return m_objectIsSet;
}

void V2MetricStatus::Unsetobject()
{
    m_objectIsSet = false;
}
std::shared_ptr<V2PodsMetricStatus> V2MetricStatus::GetPods() const
{
    return m_pods;
}

void V2MetricStatus::SetPods(const std::shared_ptr<V2PodsMetricStatus>& value)
{
    m_pods = value;
    m_podsIsSet = true;
}

bool V2MetricStatus::PodsIsSet() const
{
    return m_podsIsSet;
}

void V2MetricStatus::UnsetPods()
{
    m_podsIsSet = false;
}
std::shared_ptr<V2ResourceMetricStatus> V2MetricStatus::GetResource() const
{
    return m_resource;
}

void V2MetricStatus::SetResource(const std::shared_ptr<V2ResourceMetricStatus>& value)
{
    m_resource = value;
    m_resourceIsSet = true;
}

bool V2MetricStatus::ResourceIsSet() const
{
    return m_resourceIsSet;
}

void V2MetricStatus::UnsetResource()
{
    m_resourceIsSet = false;
}
std::string V2MetricStatus::GetType() const
{
    return m_type;
}

void V2MetricStatus::SetType(const std::string& value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V2MetricStatus::TypeIsSet() const
{
    return m_typeIsSet;
}

void V2MetricStatus::UnsetType()
{
    m_typeIsSet = false;
}
}
}
