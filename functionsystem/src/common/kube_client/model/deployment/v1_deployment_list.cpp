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

#include "v1_deployment_list.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1DeploymentList);

V1DeploymentList::V1DeploymentList()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_itemsIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_metadataIsSet = false;
}

V1DeploymentList::~V1DeploymentList()
{
}

nlohmann::json V1DeploymentList::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_metadataIsSet) {
        val["metadata"] = ModelUtils::ToJson(m_metadata);
    }
    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_itemsIsSet) {
        val["items"] = ModelUtils::ToJson(m_items);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    return val;
}

bool V1DeploymentList::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("kind")) {
        const nlohmann::json &fieldValue = val.at("kind");
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    if (val.contains("apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains("items")) {
        const nlohmann::json &fieldValue = val.at("items");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1Deployment>> refValSetItems;
            ok &= ModelUtils::FromJson(fieldValue, refValSetItems);
            SetItems(refValSetItems);
        }
    }
    if (val.contains("metadata")) {
        const nlohmann::json &fieldValue = val.at("metadata");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ListMeta> refValSetMetadata;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetadata);
            SetMetadata(refValSetMetadata);
        }
    }
    return ok;
}

std::string V1DeploymentList::GetApiVersion() const
{
    return m_apiVersion;
}

void V1DeploymentList::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1DeploymentList::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1DeploymentList::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}

std::vector<std::shared_ptr<V1Deployment>> &V1DeploymentList::GetItems()
{
    return m_items;
}

void V1DeploymentList::SetItems(const std::vector<std::shared_ptr<V1Deployment>> &value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V1DeploymentList::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V1DeploymentList::UnsetItems()
{
    m_itemsIsSet = false;
}
std::string V1DeploymentList::GetKind() const
{
    return m_kind;
}

void V1DeploymentList::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1DeploymentList::KindIsSet() const
{
    return m_kindIsSet;
}

void V1DeploymentList::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ListMeta> V1DeploymentList::GetMetadata() const
{
    return m_metadata;
}

void V1DeploymentList::SetMetadata(const std::shared_ptr<V1ListMeta> &value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1DeploymentList::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1DeploymentList::UnsetMetadata()
{
    m_metadataIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
