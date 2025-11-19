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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2CROSSVERSIONOBJECTREFERENCE_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2CROSSVERSIONOBJECTREFERENCE_H

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_CROSS_VERSION_OBJECT_REFERENCE = "V2CrossVersionObjectReference";

class V2CrossVersionObjectReference : public ModelBase {
public:
    V2CrossVersionObjectReference();
    ~V2CrossVersionObjectReference() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string& value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string& value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string& value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::string m_name;
    bool m_nameIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2CROSSVERSIONOBJECTREFERENCE_H */
