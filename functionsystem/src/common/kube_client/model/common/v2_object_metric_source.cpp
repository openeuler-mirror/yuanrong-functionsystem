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

#include "v2_object_metric_source.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2ObjectMetricSource);

V2ObjectMetricSource::V2ObjectMetricSource() noexcept : m_describedObjectIsSet(false),
    m_metricIsSet(false),
    m_targetIsSet(false)
{}

V2ObjectMetricSource::~V2ObjectMetricSource()
{
}

nlohmann::json V2ObjectMetricSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_describedObjectIsSet) {
        val[std::string(("describedObject"))] = ModelUtils::ToJson(m_describedObject);
    }
    if (m_metricIsSet) {
        val[std::string(("metric"))] = ModelUtils::ToJson(m_metric);
    }
    if (m_targetIsSet) {
        val[std::string(("target"))] = ModelUtils::ToJson(m_target);
    }

    return val;
}

bool V2ObjectMetricSource::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains(std::string(("metric")))) {
        const nlohmann::json& fieldValue = val.at(std::string("metric"));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricIdentifier> refValSetMetric;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetric);
            SetMetric(refValSetMetric);
        }
    }
    if (val.contains(std::string("describedObject"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("describedObject")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2CrossVersionObjectReference> refValSetDescribedObject;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDescribedObject);
            SetDescribedObject(refValSetDescribedObject);
        }
    }
    if (val.contains(std::string("target"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("target")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricTarget> refValSetTarget;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTarget);
            SetTarget(refValSetTarget);
        }
    }
    return ok;
}

std::shared_ptr<V2CrossVersionObjectReference> V2ObjectMetricSource::GetDescribedObject() const
{
    return m_describedObject;
}

void V2ObjectMetricSource::SetDescribedObject(const std::shared_ptr<V2CrossVersionObjectReference>& value)
{
    m_describedObject = value;
    m_describedObjectIsSet = true;
}

bool V2ObjectMetricSource::DescribedObjectIsSet() const
{
    return m_describedObjectIsSet;
}

void V2ObjectMetricSource::UnsetDescribedObject()
{
    m_describedObjectIsSet = false;
}
std::shared_ptr<V2MetricIdentifier> V2ObjectMetricSource::GetMetric() const
{
    return m_metric;
}

void V2ObjectMetricSource::SetMetric(const std::shared_ptr<V2MetricIdentifier>& value)
{
    m_metric = value;
    m_metricIsSet = true;
}

bool V2ObjectMetricSource::MetricIsSet() const
{
    return m_metricIsSet;
}

void V2ObjectMetricSource::UnsetMetric()
{
    m_metricIsSet = false;
}
std::shared_ptr<V2MetricTarget> V2ObjectMetricSource::GetTarget() const
{
    return m_target;
}

void V2ObjectMetricSource::SetTarget(const std::shared_ptr<V2MetricTarget>& value)
{
    m_target = value;
    m_targetIsSet = true;
}

bool V2ObjectMetricSource::TargetIsSet() const
{
    return m_targetIsSet;
}

void V2ObjectMetricSource::UnsetTarget()
{
    m_targetIsSet = false;
}
}
}
