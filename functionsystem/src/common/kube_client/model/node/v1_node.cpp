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

#include "v1_node.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Node);

V1Node::V1Node()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_metadataIsSet = false;
    m_specIsSet = false;
    m_statusIsSet = false;
}

V1Node::~V1Node()
{
}

nlohmann::json V1Node::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    if (m_metadataIsSet) {
        val["metadata"] = ModelUtils::ToJson(m_metadata);
    }
    if (m_specIsSet) {
        val["spec"] = ModelUtils::ToJson(m_spec);
    }
    if (m_statusIsSet) {
        val["status"] = ModelUtils::ToJson(m_status);
    }

    return val;
}

bool V1Node::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (ModelUtils::CheckHasKey(val, "kind")) {
        const nlohmann::json &fieldValue = val.at("kind");
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    if (ModelUtils::CheckHasKey(val, "apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (ModelUtils::CheckHasKey(val, "metadata")) {
        std::shared_ptr<V1ObjectMeta> refValSetMetadata;
        ok &= ModelUtils::FromJson(val.at("metadata"), refValSetMetadata);
        SetMetadata(refValSetMetadata);
    }
    if (val.contains("spec")) {
        const nlohmann::json &fieldValue = val.at("spec");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1NodeSpec> refValSetSpec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSpec);
            SetSpec(refValSetSpec);
        }
    }
    if (val.contains("status")) {
        const nlohmann::json &fieldValue = val.at("status");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1NodeStatus> refValSetStatus;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStatus);
            SetStatus(refValSetStatus);
        }
    }
    return ok;
}

std::string V1Node::GetApiVersion() const
{
    return m_apiVersion;
}

void V1Node::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1Node::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1Node::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::string V1Node::GetKind() const
{
    return m_kind;
}

void V1Node::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1Node::KindIsSet() const
{
    return m_kindIsSet;
}

void V1Node::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ObjectMeta> V1Node::GetMetadata() const
{
    return m_metadata;
}

void V1Node::SetMetadata(const std::shared_ptr<V1ObjectMeta> &value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1Node::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1Node::UnsetMetadata()
{
    m_metadataIsSet = false;
}
std::shared_ptr<V1NodeSpec> V1Node::GetSpec() const
{
    return m_spec;
}

void V1Node::SetSpec(const std::shared_ptr<V1NodeSpec> &value)
{
    m_spec = value;
    m_specIsSet = true;
}

bool V1Node::SpecIsSet() const
{
    return m_specIsSet;
}

void V1Node::UnsetSpec()
{
    m_specIsSet = false;
}

std::shared_ptr<V1NodeStatus> V1Node::GetStatus() const
{
    return m_status;
}

void V1Node::SetStatus(const std::shared_ptr<V1NodeStatus> &value)
{
    m_status = value;
    m_statusIsSet = true;
}

bool V1Node::StatusIsSet() const
{
    return m_statusIsSet;
}

void V1Node::UnsetStatus()
{
    m_statusIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
