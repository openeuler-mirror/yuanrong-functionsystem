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

#include "v1_node_list.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1NodeList);

V1NodeList::V1NodeList()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_itemsIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_metadataIsSet = false;
}

V1NodeList::~V1NodeList()
{
}

nlohmann::json V1NodeList::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_itemsIsSet) {
        val["items"] = ModelUtils::ToJson(m_items);
    }
    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    if (m_metadataIsSet) {
        val["metadata"] = ModelUtils::ToJson(m_metadata);
    }

    return val;
}

bool V1NodeList::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains("kind")) {
        const nlohmann::json &fieldValue = val.at("kind");
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    if (ModelUtils::CheckHasKey(val, "items")) {
        std::vector<std::shared_ptr<V1Node>> refValSetItems;
        ok &= ModelUtils::FromJson(val.at("items"), refValSetItems);
        SetItems(refValSetItems);
    }
    if (ModelUtils::CheckHasKey(val, "metadata")) {
        std::shared_ptr<V1ListMeta> refValSetMetadata;
        ok &= ModelUtils::FromJson(val.at("metadata"), refValSetMetadata);
        SetMetadata(refValSetMetadata);
    }
    return ok;
}

std::string V1NodeList::GetApiVersion() const
{
    return m_apiVersion;
}

void V1NodeList::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1NodeList::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1NodeList::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}

std::vector<std::shared_ptr<V1Node>> &V1NodeList::GetItems()
{
    return m_items;
}

void V1NodeList::SetItems(const std::vector<std::shared_ptr<V1Node>> &value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V1NodeList::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V1NodeList::UnsetItems()
{
    m_itemsIsSet = false;
}
std::string V1NodeList::GetKind() const
{
    return m_kind;
}

void V1NodeList::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1NodeList::KindIsSet() const
{
    return m_kindIsSet;
}

void V1NodeList::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ListMeta> V1NodeList::GetMetadata() const
{
    return m_metadata;
}

void V1NodeList::SetMetadata(const std::shared_ptr<V1ListMeta> &value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1NodeList::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1NodeList::UnsetMetadata()
{
    m_metadataIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
