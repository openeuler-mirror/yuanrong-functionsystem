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

#include "v1_list_meta.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ListMeta);

V1ListMeta::V1ListMeta()
{
    m_rContinue = std::string("");
    m_rContinueIsSet = false;
    m_remainingItemCount = 0L;
    m_remainingItemCountIsSet = false;
    m_resourceVersion = std::string("");
    m_resourceVersionIsSet = false;
    m_selfLink = std::string("");
    m_selfLinkIsSet = false;
}

V1ListMeta::~V1ListMeta()
{
}

nlohmann::json V1ListMeta::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_rContinueIsSet) {
        val["continue"] = ModelUtils::ToJson(m_rContinue);
    }
    if (m_remainingItemCountIsSet) {
        val["remainingItemCount"] = ModelUtils::ToJson(m_remainingItemCount);
    }
    if (m_resourceVersionIsSet) {
        val["resourceVersion"] = ModelUtils::ToJson(m_resourceVersion);
    }
    if (m_selfLinkIsSet) {
        val["selfLink"] = ModelUtils::ToJson(m_selfLink);
    }

    return val;
}

bool V1ListMeta::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("continue")) {
        const nlohmann::json &fieldValue = val.at("continue");
        if (!fieldValue.is_null()) {
            std::string refValSetRContinue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRContinue);
            SetRContinue(refValSetRContinue);
        }
    }
    if (val.contains("remainingItemCount")) {
        const nlohmann::json &fieldValue = val.at("remainingItemCount");
        if (!fieldValue.is_null()) {
            int64_t refValSetRemainingItemCount;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRemainingItemCount);
            SetRemainingItemCount(refValSetRemainingItemCount);
        }
    }
    if (val.contains("resourceVersion")) {
        const nlohmann::json &fieldValue = val.at("resourceVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetResourceVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetResourceVersion);
            SetResourceVersion(refValSetResourceVersion);
        }
    }
    if (val.contains("selfLink")) {
        const nlohmann::json &fieldValue = val.at("selfLink");
        if (!fieldValue.is_null()) {
            std::string refValSetSelfLink;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSelfLink);
            SetSelfLink(refValSetSelfLink);
        }
    }
    return ok;
}

std::string V1ListMeta::GetRContinue() const
{
    return m_rContinue;
}

void V1ListMeta::SetRContinue(const std::string &value)
{
    m_rContinue = value;
    m_rContinueIsSet = true;
}

bool V1ListMeta::RContinueIsSet() const
{
    return m_rContinueIsSet;
}

void V1ListMeta::UnsetrContinue()
{
    m_rContinueIsSet = false;
}
int64_t V1ListMeta::GetRemainingItemCount() const
{
    return m_remainingItemCount;
}

void V1ListMeta::SetRemainingItemCount(int64_t value)
{
    m_remainingItemCount = value;
    m_remainingItemCountIsSet = true;
}

bool V1ListMeta::RemainingItemCountIsSet() const
{
    return m_remainingItemCountIsSet;
}

void V1ListMeta::UnsetRemainingItemCount()
{
    m_remainingItemCountIsSet = false;
}
std::string V1ListMeta::GetResourceVersion() const
{
    return m_resourceVersion;
}

void V1ListMeta::SetResourceVersion(const std::string &value)
{
    m_resourceVersion = value;
    m_resourceVersionIsSet = true;
}

bool V1ListMeta::ResourceVersionIsSet() const
{
    return m_resourceVersionIsSet;
}

void V1ListMeta::UnsetResourceVersion()
{
    m_resourceVersionIsSet = false;
}
std::string V1ListMeta::GetSelfLink() const
{
    return m_selfLink;
}

void V1ListMeta::SetSelfLink(const std::string &value)
{
    m_selfLink = value;
    m_selfLinkIsSet = true;
}

bool V1ListMeta::SelfLinkIsSet() const
{
    return m_selfLinkIsSet;
}

void V1ListMeta::UnSetSelfLink()
{
    m_selfLinkIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
