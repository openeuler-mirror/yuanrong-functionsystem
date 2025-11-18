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

#include "v2_resource_metric_source.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2ResourceMetricSource);

V2ResourceMetricSource::V2ResourceMetricSource() : m_name(std::string("")),
    m_nameIsSet(false),
    m_targetIsSet(false)
{}

V2ResourceMetricSource::~V2ResourceMetricSource()
{
}

nlohmann::json V2ResourceMetricSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_nameIsSet) {
        val[std::string(("name"))] = ModelUtils::ToJson(m_name);
    }
    if (m_targetIsSet) {
        val[std::string(("target"))] = ModelUtils::ToJson(m_target);
    }

    return val;
}

bool V2ResourceMetricSource::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains(std::string(("target")))) {
        const nlohmann::json& fieldValue = val.at(std::string("target"));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricTarget> refValSetTarget;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTarget);
            SetTarget(refValSetTarget);
        }
    }
    if (val.contains(std::string(("name")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("name")));
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    return ok;
}

std::string V2ResourceMetricSource::GetName() const
{
    return m_name;
}

void V2ResourceMetricSource::SetName(const std::string& value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V2ResourceMetricSource::NameIsSet() const
{
    return m_nameIsSet;
}

void V2ResourceMetricSource::UnsetName()
{
    m_nameIsSet = false;
}
std::shared_ptr<V2MetricTarget> V2ResourceMetricSource::GetTarget() const
{
    return m_target;
}

void V2ResourceMetricSource::SetTarget(const std::shared_ptr<V2MetricTarget>& value)
{
    m_target = value;
    m_targetIsSet = true;
}

bool V2ResourceMetricSource::TargetIsSet() const
{
    return m_targetIsSet;
}

void V2ResourceMetricSource::UnsetTarget()
{
    m_targetIsSet = false;
}
}
}
