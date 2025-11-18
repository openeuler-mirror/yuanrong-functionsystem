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

#include "v1_pod_template_spec.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodTemplateSpec);

V1PodTemplateSpec::V1PodTemplateSpec()
{
    m_metadataIsSet = false;
    m_specIsSet = false;
}

V1PodTemplateSpec::~V1PodTemplateSpec()
{
}

nlohmann::json V1PodTemplateSpec::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_metadataIsSet) {
        val["metadata"] = ModelUtils::ToJson(m_metadata);
    }
    if (m_specIsSet) {
        val["spec"] = ModelUtils::ToJson(m_spec);
    }

    return val;
}

bool V1PodTemplateSpec::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "metadata")) {
        std::shared_ptr<V1ObjectMeta> refValSetMetadata;
        ok &= ModelUtils::FromJson(val.at("metadata"), refValSetMetadata);
        SetMetadata(refValSetMetadata);
    }
    if (val.contains("spec")) {
        const nlohmann::json &fieldValue = val.at("spec");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodSpec> refValSetSpec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSpec);
            SetSpec(refValSetSpec);
        }
    }
    return ok;
}

std::shared_ptr<V1ObjectMeta> V1PodTemplateSpec::GetMetadata() const
{
    return m_metadata;
}

void V1PodTemplateSpec::SetMetadata(const std::shared_ptr<V1ObjectMeta> &value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1PodTemplateSpec::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1PodTemplateSpec::UnsetMetadata()
{
    m_metadataIsSet = false;
}
std::shared_ptr<V1PodSpec> V1PodTemplateSpec::GetSpec() const
{
    return m_spec;
}

void V1PodTemplateSpec::SetSpec(const std::shared_ptr<V1PodSpec> &value)
{
    m_spec = value;
    m_specIsSet = true;
}

bool V1PodTemplateSpec::SpecIsSet() const
{
    return m_specIsSet;
}

void V1PodTemplateSpec::UnsetSpec()
{
    m_specIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
