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

#include "v2_object_metric_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2ObjectMetricStatus);

V2ObjectMetricStatus::V2ObjectMetricStatus() noexcept : m_currentIsSet(false),
    m_describedObjectIsSet(false),
    m_metricIsSet(false)
{}

V2ObjectMetricStatus::~V2ObjectMetricStatus()
{
}

nlohmann::json V2ObjectMetricStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_currentIsSet) {
        val[std::string(("current"))] = ModelUtils::ToJson(m_current);
    }
    if (m_describedObjectIsSet) {
        val[std::string(("describedObject"))] = ModelUtils::ToJson(m_describedObject);
    }
    if (m_metricIsSet) {
        val[std::string(("metric"))] = ModelUtils::ToJson(m_metric);
    }

    return val;
}

bool V2ObjectMetricStatus::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string("current"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("current")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricValueStatus> refValSetCurrent;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCurrent);
            SetCurrent(refValSetCurrent);
        }
    }
    if (val.contains(std::string(("describedObject")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("describedObject")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2CrossVersionObjectReference> refValSetDescribedObject;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDescribedObject);
            SetDescribedObject(refValSetDescribedObject);
        }
    }
    if (val.contains(std::string(("metric")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("metric")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricIdentifier> refValSetMetric;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetric);
            SetMetric(refValSetMetric);
        }
    }
    return ok;
}

std::shared_ptr<V2MetricValueStatus> V2ObjectMetricStatus::GetCurrent() const
{
    return m_current;
}

void V2ObjectMetricStatus::SetCurrent(const std::shared_ptr<V2MetricValueStatus>& value)
{
    m_current = value;
    m_currentIsSet = true;
}

bool V2ObjectMetricStatus::CurrentIsSet() const
{
    return m_currentIsSet;
}

void V2ObjectMetricStatus::UnsetCurrent()
{
    m_currentIsSet = false;
}
std::shared_ptr<V2CrossVersionObjectReference> V2ObjectMetricStatus::GetDescribedObject() const
{
    return m_describedObject;
}

void V2ObjectMetricStatus::SetDescribedObject(const std::shared_ptr<V2CrossVersionObjectReference>& value)
{
    m_describedObject = value;
    m_describedObjectIsSet = true;
}

bool V2ObjectMetricStatus::DescribedObjectIsSet() const
{
    return m_describedObjectIsSet;
}

void V2ObjectMetricStatus::UnsetDescribedObject()
{
    m_describedObjectIsSet = false;
}
std::shared_ptr<V2MetricIdentifier> V2ObjectMetricStatus::GetMetric() const
{
    return m_metric;
}

void V2ObjectMetricStatus::SetMetric(const std::shared_ptr<V2MetricIdentifier>& value)
{
    m_metric = value;
    m_metricIsSet = true;
}

bool V2ObjectMetricStatus::MetricIsSet() const
{
    return m_metricIsSet;
}

void V2ObjectMetricStatus::UnsetMetric()
{
    m_metricIsSet = false;
}
}
}
