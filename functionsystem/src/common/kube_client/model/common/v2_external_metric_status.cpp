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

#include "v2_external_metric_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2ExternalMetricStatus);

V2ExternalMetricStatus::V2ExternalMetricStatus() noexcept : m_currentIsSet(false),
    m_metricIsSet(false)
{}

V2ExternalMetricStatus::~V2ExternalMetricStatus()
{
}

nlohmann::json V2ExternalMetricStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    
    if (m_currentIsSet) {
        val[std::string("current")] = ModelUtils::ToJson(m_current);
    }
    if (m_metricIsSet) {
        val[std::string(("metric"))] = ModelUtils::ToJson(m_metric);
    }

    return val;
}

bool V2ExternalMetricStatus::FromJson(const nlohmann::json& val)
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
    if (val.contains(std::string("metric"))) {
        const nlohmann::json& fieldValue = val.at(std::string("metric"));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricIdentifier> refValSetMetric;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetric);
            SetMetric(refValSetMetric);
        }
    }
    return ok;
}

std::shared_ptr<V2MetricValueStatus> V2ExternalMetricStatus::GetCurrent() const
{
    return m_current;
}

void V2ExternalMetricStatus::SetCurrent(const std::shared_ptr<V2MetricValueStatus>& value)
{
    m_current = value;
    m_currentIsSet = true;
}

bool V2ExternalMetricStatus::CurrentIsSet() const
{
    return m_currentIsSet;
}

void V2ExternalMetricStatus::UnsetCurrent()
{
    m_currentIsSet = false;
}
std::shared_ptr<V2MetricIdentifier> V2ExternalMetricStatus::GetMetric() const
{
    return m_metric;
}

void V2ExternalMetricStatus::SetMetric(const std::shared_ptr<V2MetricIdentifier>& value)
{
    m_metric = value;
    m_metricIsSet = true;
}

bool V2ExternalMetricStatus::MetricIsSet() const
{
    return m_metricIsSet;
}

void V2ExternalMetricStatus::UnsetMetric()
{
    m_metricIsSet = false;
}
}
}
