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

#include "v2_horizontal_pod_autoscaler_list.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HorizontalPodAutoscalerList);

V2HorizontalPodAutoscalerList::V2HorizontalPodAutoscalerList() : m_apiVersion(std::string("")),
    m_apiVersionIsSet(false),
    m_itemsIsSet(false),
    m_kind(std::string("")),
    m_kindIsSet(false),
    m_metadataIsSet(false)
{}

V2HorizontalPodAutoscalerList::~V2HorizontalPodAutoscalerList()
{
}


nlohmann::json V2HorizontalPodAutoscalerList::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val[std::string(("apiVersion"))] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_itemsIsSet) {
        val[std::string(("items"))] = ModelUtils::ToJson(m_items);
    }
    if (m_kindIsSet) {
        val[std::string(("kind"))] = ModelUtils::ToJson(m_kind);
    }
    if (m_metadataIsSet) {
        val[std::string(("metadata"))] = ModelUtils::ToJson(m_metadata);
    }

    return val;
}

bool V2HorizontalPodAutoscalerList::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains(std::string(("items")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("items")));
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V2HorizontalPodAutoscaler>> refValSetItems;
            ok &= ModelUtils::FromJson(fieldValue, refValSetItems);
            SetItems(refValSetItems);
        }
    }
    if (val.contains(std::string(("metadata")))) {
        const nlohmann::json& fieldValue = val.at(std::string("metadata"));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ListMeta> refValSetMetadata;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetadata);
            SetMetadata(refValSetMetadata);
        }
    }
    if (val.contains(std::string(("apiVersion")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("apiVersion")));
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains(std::string(("kind")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("kind")));
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    return ok;
}

std::string V2HorizontalPodAutoscalerList::GetApiVersion() const
{
    return m_apiVersion;
}

void V2HorizontalPodAutoscalerList::SetApiVersion(const std::string& value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V2HorizontalPodAutoscalerList::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V2HorizontalPodAutoscalerList::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::vector<std::shared_ptr<V2HorizontalPodAutoscaler>>& V2HorizontalPodAutoscalerList::GetItems()
{
    return m_items;
}

void V2HorizontalPodAutoscalerList::SetItems(const std::vector<std::shared_ptr<V2HorizontalPodAutoscaler>>& value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V2HorizontalPodAutoscalerList::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V2HorizontalPodAutoscalerList::UnsetItems()
{
    m_itemsIsSet = false;
}
std::string V2HorizontalPodAutoscalerList::GetKind() const
{
    return m_kind;
}

void V2HorizontalPodAutoscalerList::SetKind(const std::string& value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V2HorizontalPodAutoscalerList::KindIsSet() const
{
    return m_kindIsSet;
}

void V2HorizontalPodAutoscalerList::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ListMeta> V2HorizontalPodAutoscalerList::GetMetadata() const
{
    return m_metadata;
}

void V2HorizontalPodAutoscalerList::SetMetadata(const std::shared_ptr<V1ListMeta>& value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V2HorizontalPodAutoscalerList::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V2HorizontalPodAutoscalerList::UnsetMetadata()
{
    m_metadataIsSet = false;
}
}
}