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

#include "v2_container_resource_metric_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2ContainerResourceMetricStatus);

V2ContainerResourceMetricStatus::V2ContainerResourceMetricStatus() : m_container(std::string("")),
    m_containerIsSet(false),
    m_currentIsSet(false),
    m_name(std::string("")),
    m_nameIsSet(false)
{}

V2ContainerResourceMetricStatus::~V2ContainerResourceMetricStatus()
{
}

nlohmann::json V2ContainerResourceMetricStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerIsSet) {
        val[std::string(("container"))] = ModelUtils::ToJson(m_container);
    }
    if (m_currentIsSet) {
        val[std::string(("current"))] = ModelUtils::ToJson(m_current);
    }
    if (m_nameIsSet) {
        val[std::string(("name"))] = ModelUtils::ToJson(m_name);
    }

    return val;
}

bool V2ContainerResourceMetricStatus::FromJson(const nlohmann::json& val)
{
    bool ok = true;

    if (val.contains(std::string(("container")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("container")));
        if (!fieldValue.is_null()) {
            std::string refValSetContainer;
            ok &= ModelUtils::FromJson(fieldValue, refValSetContainer);
            SetContainer(refValSetContainer);
        }
    }
    if (val.contains(std::string("current"))) {
        const nlohmann::json& fieldValue = val.at(std::string("current"));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricValueStatus> refValSetCurrent;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCurrent);
            SetCurrent(refValSetCurrent);
        }
    }
    if (val.contains(std::string("name"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("name")));
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    return ok;
}

std::string V2ContainerResourceMetricStatus::GetContainer() const
{
    return m_container;
}

void V2ContainerResourceMetricStatus::SetContainer(const std::string& value)
{
    m_container = value;
    m_containerIsSet = true;
}

bool V2ContainerResourceMetricStatus::ContainerIsSet() const
{
    return m_containerIsSet;
}

void V2ContainerResourceMetricStatus::UnsetContainer()
{
    m_containerIsSet = false;
}
std::shared_ptr<V2MetricValueStatus> V2ContainerResourceMetricStatus::GetCurrent() const
{
    return m_current;
}

void V2ContainerResourceMetricStatus::SetCurrent(const std::shared_ptr<V2MetricValueStatus>& value)
{
    m_current = value;
    m_currentIsSet = true;
}

bool V2ContainerResourceMetricStatus::CurrentIsSet() const
{
    return m_currentIsSet;
}

void V2ContainerResourceMetricStatus::UnsetCurrent()
{
    m_currentIsSet = false;
}
std::string V2ContainerResourceMetricStatus::GetName() const
{
    return m_name;
}

void V2ContainerResourceMetricStatus::SetName(const std::string& value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V2ContainerResourceMetricStatus::NameIsSet() const
{
    return m_nameIsSet;
}

void V2ContainerResourceMetricStatus::UnsetName()
{
    m_nameIsSet = false;
}
}
}
