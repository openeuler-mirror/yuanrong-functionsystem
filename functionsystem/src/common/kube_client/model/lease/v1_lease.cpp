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

#include "v1_lease.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Lease);

V1Lease::V1Lease()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_metadataIsSet = false;
    m_specIsSet = false;
}

V1Lease::~V1Lease()
{
}

nlohmann::json V1Lease::ToJson() const
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
    return val;
}

bool V1Lease::FromJson(const nlohmann::json& val)
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
            std::shared_ptr<V1LeaseSpec> refValSetSpec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSpec);
            SetSpec(refValSetSpec);
        }
    }
    return ok;
}

std::string V1Lease::GetApiVersion() const
{
    return m_apiVersion;
}

void V1Lease::SetApiVersion(const std::string& value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1Lease::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1Lease::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::string V1Lease::GetKind() const
{
    return m_kind;
}

void V1Lease::SetKind(const std::string& value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1Lease::KindIsSet() const
{
    return m_kindIsSet;
}

void V1Lease::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ObjectMeta> V1Lease::GetMetadata() const
{
    return m_metadata;
}

void V1Lease::SetMetadata(const std::shared_ptr<V1ObjectMeta>& value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1Lease::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1Lease::UnsetMetadata()
{
    m_metadataIsSet = false;
}
std::shared_ptr<V1LeaseSpec> V1Lease::GetSpec() const
{
    return m_spec;
}

void V1Lease::SetSpec(const std::shared_ptr<V1LeaseSpec>& value)
{
    m_spec = value;
    m_specIsSet = true;
}

bool V1Lease::SpecIsSet() const
{
    return m_specIsSet;
}

void V1Lease::UnsetSpec()
{
    m_specIsSet = false;
}
}
}
