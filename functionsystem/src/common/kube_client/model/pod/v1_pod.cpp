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

#include "v1_pod.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Pod);

V1Pod::V1Pod()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_metadataIsSet = false;
    m_specIsSet = false;
    m_statusIsSet = false;
}

V1Pod::~V1Pod()
{
}

nlohmann::json V1Pod::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_specIsSet) {
        val["spec"] = ModelUtils::ToJson(m_spec);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    if (m_metadataIsSet) {
        val["metadata"] = ModelUtils::ToJson(m_metadata);
    }
    if (m_statusIsSet) {
        val["status"] = ModelUtils::ToJson(m_status);
    }

    return val;
}

bool V1Pod::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        std::string refValSetApiVersion;
        ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
        SetApiVersion(refValSetApiVersion);
    }
    if (ModelUtils::CheckHasKey(val, "kind")) {
        const nlohmann::json &fieldValue = val.at("kind");
        std::string refValSetKind;
        ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
        SetKind(refValSetKind);
    }
    if (val.contains("metadata")) {
        const nlohmann::json &fieldValue = val.at("metadata");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ObjectMeta> refValSetMetadata;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetadata);
            SetMetadata(refValSetMetadata);
        }
    }
    if (val.contains("spec")) {
        const nlohmann::json &fieldValue = val.at("spec");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodSpec> refValSetSpec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSpec);
            SetSpec(refValSetSpec);
        }
    }
    if (val.contains("status")) {
        const nlohmann::json &fieldValue = val.at("status");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodStatus> refValSetStatus;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStatus);
            SetStatus(refValSetStatus);
        }
    }
    return ok;
}

std::string V1Pod::GetApiVersion() const
{
    return m_apiVersion;
}

void V1Pod::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1Pod::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1Pod::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::string V1Pod::GetKind() const
{
    return m_kind;
}

void V1Pod::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1Pod::KindIsSet() const
{
    return m_kindIsSet;
}

void V1Pod::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ObjectMeta> V1Pod::GetMetadata() const
{
    return m_metadata;
}

void V1Pod::SetMetadata(const std::shared_ptr<V1ObjectMeta> &value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1Pod::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1Pod::UnsetMetadata()
{
    m_metadataIsSet = false;
}
std::shared_ptr<V1PodSpec> V1Pod::GetSpec() const
{
    return m_spec;
}

void V1Pod::SetSpec(const std::shared_ptr<V1PodSpec> &value)
{
    m_spec = value;
    m_specIsSet = true;
}

bool V1Pod::SpecIsSet() const
{
    return m_specIsSet;
}

void V1Pod::UnsetSpec()
{
    m_specIsSet = false;
}
std::shared_ptr<V1PodStatus> V1Pod::GetStatus() const
{
    return m_status;
}

void V1Pod::SetStatus(const std::shared_ptr<V1PodStatus> &value)
{
    m_status = value;
    m_statusIsSet = true;
}

bool V1Pod::StatusIsSet() const
{
    return m_statusIsSet;
}

void V1Pod::UnsetStatus()
{
    m_statusIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
