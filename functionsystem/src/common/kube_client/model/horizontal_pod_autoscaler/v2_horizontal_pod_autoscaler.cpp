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

#include "v2_horizontal_pod_autoscaler.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HorizontalPodAutoscaler);

V2HorizontalPodAutoscaler::V2HorizontalPodAutoscaler() : m_apiVersion(std::string("")),
    m_apiVersionIsSet(false),
    m_kind(std::string("")),
    m_kindIsSet(false),
    m_metadataIsSet(false),
    m_specIsSet(false),
    m_statusIsSet(false)
{}

V2HorizontalPodAutoscaler::~V2HorizontalPodAutoscaler()
{
}

nlohmann::json V2HorizontalPodAutoscaler::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val[std::string(("apiVersion"))] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_kindIsSet) {
        val[std::string(("kind"))] = ModelUtils::ToJson(m_kind);
    }
    if (m_metadataIsSet) {
        val[std::string(("metadata"))] = ModelUtils::ToJson(m_metadata);
    }
    if (m_specIsSet) {
        val[std::string(("spec"))] = ModelUtils::ToJson(m_spec);
    }
    if (m_statusIsSet) {
        val[std::string(("status"))] = ModelUtils::ToJson(m_status);
    }

    return val;
}

bool V2HorizontalPodAutoscaler::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains(std::string(("kind")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("kind")));
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
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
    if (val.contains(std::string(("metadata")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("metadata")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ObjectMeta> refValSetMetadata;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetadata);
            SetMetadata(refValSetMetadata);
        }
    }
    if (val.contains(std::string(("spec")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("spec")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2HorizontalPodAutoscalerSpec> refValSetSpec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSpec);
            SetSpec(refValSetSpec);
        }
    }
    if (val.contains(std::string(("status")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("status")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2HorizontalPodAutoscalerStatus> refValSetStatus;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStatus);
            SetStatus(refValSetStatus);
        }
    }
    return ok;
}

std::string V2HorizontalPodAutoscaler::GetApiVersion() const
{
    return m_apiVersion;
}

void V2HorizontalPodAutoscaler::SetApiVersion(const std::string& value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V2HorizontalPodAutoscaler::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V2HorizontalPodAutoscaler::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::string V2HorizontalPodAutoscaler::GetKind() const
{
    return m_kind;
}

void V2HorizontalPodAutoscaler::SetKind(const std::string& value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V2HorizontalPodAutoscaler::KindIsSet() const
{
    return m_kindIsSet;
}

void V2HorizontalPodAutoscaler::UnsetKind()
{
    m_kindIsSet = false;
}
std::shared_ptr<V1ObjectMeta> V2HorizontalPodAutoscaler::GetMetadata() const
{
    return m_metadata;
}

void V2HorizontalPodAutoscaler::SetMetadata(const std::shared_ptr<V1ObjectMeta>& value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V2HorizontalPodAutoscaler::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V2HorizontalPodAutoscaler::UnsetMetadata()
{
    m_metadataIsSet = false;
}
std::shared_ptr<V2HorizontalPodAutoscalerSpec> V2HorizontalPodAutoscaler::GetSpec() const
{
    return m_spec;
}

void V2HorizontalPodAutoscaler::SetSpec(const std::shared_ptr<V2HorizontalPodAutoscalerSpec>& value)
{
    m_spec = value;
    m_specIsSet = true;
}

bool V2HorizontalPodAutoscaler::SpecIsSet() const
{
    return m_specIsSet;
}

void V2HorizontalPodAutoscaler::UnsetSpec()
{
    m_specIsSet = false;
}
std::shared_ptr<V2HorizontalPodAutoscalerStatus> V2HorizontalPodAutoscaler::GetStatus() const
{
    return m_status;
}

void V2HorizontalPodAutoscaler::SetStatus(const std::shared_ptr<V2HorizontalPodAutoscalerStatus>& value)
{
    m_status = value;
    m_statusIsSet = true;
}

bool V2HorizontalPodAutoscaler::StatusIsSet() const
{
    return m_statusIsSet;
}

void V2HorizontalPodAutoscaler::UnsetStatus()
{
    m_statusIsSet = false;
}
}
}