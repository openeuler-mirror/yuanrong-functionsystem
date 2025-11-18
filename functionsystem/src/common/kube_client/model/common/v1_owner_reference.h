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

/*
 * V1OwnerReference.h
 *
 * OwnerReference contains enough information to let you identify an owning object. An owning object must be in the same
 * namespace as the dependent, or be cluster-scoped, so there is no namespace field.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1OWNER_REFERENCE_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1OWNER_REFERENCE_H

#include <string>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_OWNER_REFERENCE = "V1OwnerReference";

class V1OwnerReference : public ModelBase {
public:
    V1OwnerReference();
    ~V1OwnerReference() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string &value);

    bool IsBlockOwnerDeletion() const;
    bool BlockOwnerDeletionIsSet() const;
    void UnsetBlockOwnerDeletion();
    void SetBlockOwnerDeletion(bool value);

    bool IsController() const;
    bool ControllerIsSet() const;
    void UnsetController();
    void SetController(bool value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    std::string GetUid() const;
    bool UidIsSet() const;
    void UnsetUid();
    void SetUid(const std::string &value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    bool m_blockOwnerDeletion;
    bool m_blockOwnerDeletionIsSet;
    bool m_controller;
    bool m_controllerIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::string m_name;
    bool m_nameIsSet;
    std::string m_uid;
    bool m_uidIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1OWNER_REFERENCE_H */
