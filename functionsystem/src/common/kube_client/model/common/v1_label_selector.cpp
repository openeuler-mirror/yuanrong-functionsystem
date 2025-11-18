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

#include "v1_label_selector.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1LabelSelector);

V1LabelSelector::V1LabelSelector()
{
    m_matchExpressionsIsSet = false;
    m_matchLabelsIsSet = false;
}

V1LabelSelector::~V1LabelSelector()
{
}

nlohmann::json V1LabelSelector::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_matchExpressionsIsSet) {
        val["matchExpressions"] = ModelUtils::ToJson(m_matchExpressions);
    }
    if (m_matchLabelsIsSet) {
        val["matchLabels"] = ModelUtils::ToJson(m_matchLabels);
    }

    return val;
}

bool V1LabelSelector::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("matchExpressions")) {
        const nlohmann::json &fieldValue = val.at("matchExpressions");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1LabelSelectorRequirement>> refValSetMatchExpressions;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMatchExpressions);
            SetMatchExpressions(refValSetMatchExpressions);
        }
    }
    if (val.contains("matchLabels")) {
        const nlohmann::json &fieldValue = val.at("matchLabels");
        if (!fieldValue.is_null()) {
            std::map<std::string, std::string> refValSetMatchLabels;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMatchLabels);
            SetMatchLabels(refValSetMatchLabels);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V1LabelSelectorRequirement>> &V1LabelSelector::GetMatchExpressions()
{
    return m_matchExpressions;
}

void V1LabelSelector::SetMatchExpressions(const std::vector<std::shared_ptr<V1LabelSelectorRequirement>> &value)
{
    m_matchExpressions = value;
    m_matchExpressionsIsSet = true;
}

bool V1LabelSelector::MatchExpressionsIsSet() const
{
    return m_matchExpressionsIsSet;
}

void V1LabelSelector::UnsetMatchExpressions()
{
    m_matchExpressionsIsSet = false;
}

std::map<std::string, std::string> &V1LabelSelector::GetMatchLabels()
{
    return m_matchLabels;
}

void V1LabelSelector::SetMatchLabels(const std::map<std::string, std::string> &value)
{
    m_matchLabels = value;
    m_matchLabelsIsSet = true;
}

bool V1LabelSelector::MatchLabelsIsSet() const
{
    return m_matchLabelsIsSet;
}

void V1LabelSelector::UnsetMatchLabels()
{
    m_matchLabelsIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
