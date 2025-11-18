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

#include "v1_pod_list.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodList);

V1PodList::V1PodList()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_itemsIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_metadataIsSet = false;
}

V1PodList::~V1PodList()
{
}

nlohmann::json V1PodList::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_itemsIsSet) {
        val["items"] = ModelUtils::ToJson(m_items);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    if (m_metadataIsSet) {
        val["metadata"] = ModelUtils::ToJson(m_metadata);
    }
    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }

    return val;
}

bool V1PodList::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "apiVersion")) {
        std::string refValSetApiVersion;
        ok &= ModelUtils::FromJson(val.at("apiVersion"), refValSetApiVersion);
        SetApiVersion(refValSetApiVersion);
    }
    if (ModelUtils::CheckHasKey(val, "items")) {
        std::vector<std::shared_ptr<V1Pod>> refValSetItems;
        ok &= ModelUtils::FromJson(val.at("items"), refValSetItems);
        SetItems(refValSetItems);
    }
    if (ModelUtils::CheckHasKey(val, "kind")) {
        std::string refValSetKind;
        ok &= ModelUtils::FromJson(val.at("kind"), refValSetKind);
        SetKind(refValSetKind);
    }
    if (ModelUtils::CheckHasKey(val, "metadata")) {
        std::shared_ptr<V1ListMeta> refValSetMetadata;
        ok &= ModelUtils::FromJson(val.at("metadata"), refValSetMetadata);
        SetMetadata(refValSetMetadata);
    }
    return ok;
}

std::string V1PodList::GetApiVersion() const
{
    return m_apiVersion;
}

void V1PodList::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1PodList::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1PodList::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}

std::vector<std::shared_ptr<V1Pod>> &V1PodList::GetItems()
{
    return m_items;
}

void V1PodList::SetItems(const std::vector<std::shared_ptr<V1Pod>> &value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V1PodList::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V1PodList::UnsetItems()
{
    m_itemsIsSet = false;
}
std::string V1PodList::GetKind() const
{
    return m_kind;
}

void V1PodList::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1PodList::KindIsSet() const
{
    return m_kindIsSet;
}

void V1PodList::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ListMeta> V1PodList::GetMetadata() const
{
    return m_metadata;
}

void V1PodList::SetMetadata(const std::shared_ptr<V1ListMeta> &value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1PodList::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1PodList::UnsetMetadata()
{
    m_metadataIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
