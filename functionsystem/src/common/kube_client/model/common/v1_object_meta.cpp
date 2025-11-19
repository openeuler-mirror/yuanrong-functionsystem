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

#include "v1_object_meta.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ObjectMeta);

V1ObjectMeta::V1ObjectMeta()
{
    m_annotationsIsSet = false;
    m_creationTimestamp = utility::Datetime();
    m_creationTimestampIsSet = false;
    m_deletionTimestamp = utility::Datetime();
    m_deletionTimestampIsSet = false;
    m_finalizersIsSet = false;
    m_generateName = std::string("");
    m_generateNameIsSet = false;
    m_generation = 0L;
    m_generationIsSet = false;
    m_labelsIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_rNamespace = std::string("");
    m_rNamespaceIsSet = false;
    m_ownerReferencesIsSet = false;
    m_resourceVersion = std::string("");
    m_resourceVersionIsSet = false;
    m_selfLink = std::string("");
    m_selfLinkIsSet = false;
    m_uid = std::string("");
    m_uidIsSet = false;
}

V1ObjectMeta::~V1ObjectMeta()
{
}

nlohmann::json V1ObjectMeta::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_annotationsIsSet) {
        val["annotations"] = ModelUtils::ToJson(m_annotations);
    }
    if (m_creationTimestampIsSet) {
        val["creationTimestamp"] = ModelUtils::ToJson(m_creationTimestamp);
    }
    if (m_deletionTimestampIsSet) {
        val["deletionTimestamp"] = ModelUtils::ToJson(m_deletionTimestamp);
    }
    if (m_finalizersIsSet) {
        val["finalizers"] = ModelUtils::ToJson(m_finalizers);
    }
    if (m_generateNameIsSet) {
        val["generateName"] = ModelUtils::ToJson(m_generateName);
    }
    if (m_generationIsSet) {
        val["generation"] = ModelUtils::ToJson(m_generation);
    }
    if (m_labelsIsSet) {
        val["labels"] = ModelUtils::ToJson(m_labels);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_rNamespaceIsSet) {
        val["namespace"] = ModelUtils::ToJson(m_rNamespace);
    }
    if (m_ownerReferencesIsSet) {
        val["ownerReferences"] = ModelUtils::ToJson(m_ownerReferences);
    }
    if (m_resourceVersionIsSet) {
        val["resourceVersion"] = ModelUtils::ToJson(m_resourceVersion);
    }
    if (m_selfLinkIsSet) {
        val["selfLink"] = ModelUtils::ToJson(m_selfLink);
    }
    if (m_uidIsSet) {
        val["uid"] = ModelUtils::ToJson(m_uid);
    }

    return val;
}

bool V1ObjectMeta::FromJson(const nlohmann::json &val)
{
    bool ok = ParseBaseFromJson(val);
    if (val.contains("annotations")) {
        const nlohmann::json &fieldValue = val.at("annotations");
        if (!fieldValue.is_null()) {
            std::map<std::string, std::string> refValSetAnnotations;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAnnotations);
            SetAnnotations(refValSetAnnotations);
        }
    }
    if (val.contains("creationTimestamp")) {
        const nlohmann::json &fieldValue = val.at("creationTimestamp");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetCreationTimestamp;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCreationTimestamp);
            SetCreationTimestamp(refValSetCreationTimestamp);
        }
    }
    if (val.contains("finalizers")) {
        const nlohmann::json &fieldValue = val.at("finalizers");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetFinalizers;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFinalizers);
            SetFinalizers(refValSetFinalizers);
        }
    }
    if (val.contains("generateName")) {
        const nlohmann::json &fieldValue = val.at("generateName");
        if (!fieldValue.is_null()) {
            std::string refValSetGenerateName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetGenerateName);
            SetGenerateName(refValSetGenerateName);
        }
    }
    if (val.contains("generation")) {
        const nlohmann::json &fieldValue = val.at("generation");
        if (!fieldValue.is_null()) {
            int64_t refValSetGeneration;
            ok &= ModelUtils::FromJson(fieldValue, refValSetGeneration);
            SetGeneration(refValSetGeneration);
        }
    }
    if (val.contains("labels")) {
        const nlohmann::json &fieldValue = val.at("labels");
        if (!fieldValue.is_null()) {
            std::map<std::string, std::string> refValSetLabels;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLabels);
            SetLabels(refValSetLabels);
        }
    }

    return ok;
}

bool V1ObjectMeta::ParseBaseFromJson(const nlohmann::json &json)
{
    bool ok = true;
    if (ModelUtils::CheckHasKey(json, "name")) {
        const nlohmann::json &fieldValue = json.at("name");
        std::string refValSetName;
        ok &= ModelUtils::FromJson(fieldValue, refValSetName);
        SetName(refValSetName);
    }
    if (json.contains("namespace")) {
        const nlohmann::json &fieldValue = json.at("namespace");
        if (!fieldValue.is_null()) {
            std::string refValSetRNamespace;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRNamespace);
            SetRNamespace(refValSetRNamespace);
        }
    }
    if (json.contains("ownerReferences")) {
        const nlohmann::json &fieldValue = json.at("ownerReferences");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1OwnerReference>> refValSetOwnerReferences;
            ok &= ModelUtils::FromJson(fieldValue, refValSetOwnerReferences);
            SetOwnerReferences(refValSetOwnerReferences);
        }
    }
    if (ModelUtils::CheckHasKey(json, "resourceVersion")) {
        const nlohmann::json &fieldValue = json.at("resourceVersion");
        std::string refValSetResourceVersion;
        ok &= ModelUtils::FromJson(fieldValue, refValSetResourceVersion);
        SetResourceVersion(refValSetResourceVersion);
    }
    if (ModelUtils::CheckHasKey(json, "selfLink")) {
        const nlohmann::json &fieldValue = json.at("selfLink");
        std::string refValSetSelfLink;
        ok &= ModelUtils::FromJson(fieldValue, refValSetSelfLink);
        SetSelfLink(refValSetSelfLink);
    }
    if (json.contains("uid")) {
        const nlohmann::json &fieldValue = json.at("uid");
        if (!fieldValue.is_null()) {
            std::string refValSetUid;
            ok &= ModelUtils::FromJson(fieldValue, refValSetUid);
            SetUid(refValSetUid);
        }
    }
    return ok;
}

std::map<std::string, std::string> &V1ObjectMeta::GetAnnotations()
{
    return m_annotations;
}

void V1ObjectMeta::SetAnnotations(const std::map<std::string, std::string> &value)
{
    m_annotations = value;
    m_annotationsIsSet = true;
}

bool V1ObjectMeta::AnnotationsIsSet() const
{
    return m_annotationsIsSet;
}

void V1ObjectMeta::UnsetAnnotations()
{
    m_annotationsIsSet = false;
}
utility::Datetime V1ObjectMeta::GetCreationTimestamp() const
{
    return m_creationTimestamp;
}

void V1ObjectMeta::SetCreationTimestamp(const utility::Datetime &value)
{
    m_creationTimestamp = value;
    m_creationTimestampIsSet = true;
}

bool V1ObjectMeta::CreationTimestampIsSet() const
{
    return m_creationTimestampIsSet;
}

void V1ObjectMeta::UnsetCreationTimestamp()
{
    m_creationTimestampIsSet = false;
}

utility::Datetime V1ObjectMeta::GetDeletionTimestamp() const
{
    return m_deletionTimestamp;
}

void V1ObjectMeta::SetDeletionTimestamp(const utility::Datetime &value)
{
    m_deletionTimestamp = value;
    m_deletionTimestampIsSet = true;
}

bool V1ObjectMeta::DeletionTimestampIsSet() const
{
    return m_deletionTimestampIsSet;
}

void V1ObjectMeta::UnsetDeletionTimestamp()
{
    m_deletionTimestampIsSet = false;
}

std::vector<std::string> &V1ObjectMeta::GetFinalizers()
{
    return m_finalizers;
}

void V1ObjectMeta::SetFinalizers(const std::vector<std::string> &value)
{
    m_finalizers = value;
    m_finalizersIsSet = true;
}

bool V1ObjectMeta::FinalizersIsSet() const
{
    return m_finalizersIsSet;
}

void V1ObjectMeta::UnsetFinalizers()
{
    m_finalizersIsSet = false;
}
std::string V1ObjectMeta::GetGenerateName() const
{
    return m_generateName;
}

void V1ObjectMeta::SetGenerateName(const std::string &value)
{
    m_generateName = value;
    m_generateNameIsSet = true;
}

bool V1ObjectMeta::GenerateNameIsSet() const
{
    return m_generateNameIsSet;
}

void V1ObjectMeta::UnsetGenerateName()
{
    m_generateNameIsSet = false;
}
int64_t V1ObjectMeta::GetGeneration() const
{
    return m_generation;
}

void V1ObjectMeta::SetGeneration(int64_t value)
{
    m_generation = value;
    m_generationIsSet = true;
}

bool V1ObjectMeta::GenerationIsSet() const
{
    return m_generationIsSet;
}

void V1ObjectMeta::UnsetGeneration()
{
    m_generationIsSet = false;
}

std::map<std::string, std::string> &V1ObjectMeta::GetLabels()
{
    return m_labels;
}

void V1ObjectMeta::SetLabels(const std::map<std::string, std::string> &value)
{
    m_labels = value;
    m_labelsIsSet = true;
}

bool V1ObjectMeta::LabelsIsSet() const
{
    return m_labelsIsSet;
}

void V1ObjectMeta::UnsetLabels()
{
    m_labelsIsSet = false;
}
std::string V1ObjectMeta::GetName() const
{
    return m_name;
}

void V1ObjectMeta::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1ObjectMeta::NameIsSet() const
{
    return m_nameIsSet;
}

void V1ObjectMeta::UnsetName()
{
    m_nameIsSet = false;
}
std::string V1ObjectMeta::GetRNamespace() const
{
    return m_rNamespace;
}

void V1ObjectMeta::SetRNamespace(const std::string &value)
{
    m_rNamespace = value;
    m_rNamespaceIsSet = true;
}

bool V1ObjectMeta::RNamespaceIsSet() const
{
    return m_rNamespaceIsSet;
}

void V1ObjectMeta::UnsetRNamespace()
{
    m_rNamespaceIsSet = false;
}

std::vector<std::shared_ptr<V1OwnerReference>> &V1ObjectMeta::GetOwnerReferences()
{
    return m_ownerReferences;
}

void V1ObjectMeta::SetOwnerReferences(const std::vector<std::shared_ptr<V1OwnerReference>> &value)
{
    m_ownerReferences = value;
    m_ownerReferencesIsSet = true;
}

bool V1ObjectMeta::OwnerReferencesIsSet() const
{
    return m_ownerReferencesIsSet;
}

void V1ObjectMeta::UnsetOwnerReferences()
{
    m_ownerReferencesIsSet = false;
}
std::string V1ObjectMeta::GetResourceVersion() const
{
    return m_resourceVersion;
}

void V1ObjectMeta::SetResourceVersion(const std::string &value)
{
    m_resourceVersion = value;
    m_resourceVersionIsSet = true;
}

bool V1ObjectMeta::ResourceVersionIsSet() const
{
    return m_resourceVersionIsSet;
}

void V1ObjectMeta::UnsetResourceVersion()
{
    m_resourceVersionIsSet = false;
}
std::string V1ObjectMeta::GetSelfLink() const
{
    return m_selfLink;
}

void V1ObjectMeta::SetSelfLink(const std::string &value)
{
    m_selfLink = value;
    m_selfLinkIsSet = true;
}

bool V1ObjectMeta::SelfLinkIsSet() const
{
    return m_selfLinkIsSet;
}

void V1ObjectMeta::UnSetSelfLink()
{
    m_selfLinkIsSet = false;
}
std::string V1ObjectMeta::GetUid() const
{
    return m_uid;
}

void V1ObjectMeta::SetUid(const std::string &value)
{
    m_uid = value;
    m_uidIsSet = true;
}

bool V1ObjectMeta::UidIsSet() const
{
    return m_uidIsSet;
}

void V1ObjectMeta::UnsetUid()
{
    m_uidIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
