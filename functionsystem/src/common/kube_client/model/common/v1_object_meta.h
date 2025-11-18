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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_V1OBJECT_META_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_V1OBJECT_META_H

#include <map>
#include <string>
#include <vector>

#include "common/kube_client/api/api_exception.h"
#include "model_base.h"
#include "v1_owner_reference.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_OBJECT_META = "V1ObjectMeta";

class V1ObjectMeta : public ModelBase {
public:
    V1ObjectMeta();
    ~V1ObjectMeta() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ParseBaseFromJson(const nlohmann::json &json);

    std::map<std::string, std::string> &GetAnnotations();
    bool AnnotationsIsSet() const;
    void UnsetAnnotations();
    void SetAnnotations(const std::map<std::string, std::string> &value);

    utility::Datetime GetCreationTimestamp() const;
    bool CreationTimestampIsSet() const;
    void UnsetCreationTimestamp();
    void SetCreationTimestamp(const utility::Datetime &value);

    utility::Datetime GetDeletionTimestamp() const;
    bool DeletionTimestampIsSet() const;
    void UnsetDeletionTimestamp();
    void SetDeletionTimestamp(const utility::Datetime &value);

    std::vector<std::string> &GetFinalizers();
    bool FinalizersIsSet() const;
    void UnsetFinalizers();
    void SetFinalizers(const std::vector<std::string> &value);

    std::string GetGenerateName() const;
    bool GenerateNameIsSet() const;
    void UnsetGenerateName();
    void SetGenerateName(const std::string &value);

    int64_t GetGeneration() const;
    bool GenerationIsSet() const;
    void UnsetGeneration();
    void SetGeneration(int64_t value);

    std::map<std::string, std::string> &GetLabels();
    bool LabelsIsSet() const;
    void UnsetLabels();
    void SetLabels(const std::map<std::string, std::string> &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    std::string GetRNamespace() const;
    bool RNamespaceIsSet() const;
    void UnsetRNamespace();
    void SetRNamespace(const std::string &value);

    std::vector<std::shared_ptr<V1OwnerReference>> &GetOwnerReferences();
    bool OwnerReferencesIsSet() const;
    void UnsetOwnerReferences();
    void SetOwnerReferences(const std::vector<std::shared_ptr<V1OwnerReference>> &value);

    std::string GetResourceVersion() const;
    bool ResourceVersionIsSet() const;
    void UnsetResourceVersion();
    void SetResourceVersion(const std::string &value);

    std::string GetSelfLink() const;
    bool SelfLinkIsSet() const;
    void UnSetSelfLink();
    void SetSelfLink(const std::string &value);

    std::string GetUid() const;
    bool UidIsSet() const;
    void UnsetUid();
    void SetUid(const std::string &value);

protected:
    std::map<std::string, std::string> m_annotations;
    bool m_annotationsIsSet;
    utility::Datetime m_creationTimestamp;
    bool m_creationTimestampIsSet;
    utility::Datetime m_deletionTimestamp;
    bool m_deletionTimestampIsSet;
    std::vector<std::string> m_finalizers;
    bool m_finalizersIsSet;
    std::string m_generateName;
    bool m_generateNameIsSet;
    int64_t m_generation;
    bool m_generationIsSet;
    std::map<std::string, std::string> m_labels;
    bool m_labelsIsSet;
    std::string m_name;
    bool m_nameIsSet;
    std::string m_rNamespace;
    bool m_rNamespaceIsSet;
    std::vector<std::shared_ptr<V1OwnerReference>> m_ownerReferences;
    bool m_ownerReferencesIsSet;
    std::string m_resourceVersion;
    bool m_resourceVersionIsSet;
    std::string m_selfLink;
    bool m_selfLinkIsSet;
    std::string m_uid;
    bool m_uidIsSet;
};
}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_V1OBJECT_META_H */
