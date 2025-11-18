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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIST_META_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIST_META_H_

#include <string>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_LIST_META = "V1ListMeta";

class V1ListMeta : public ModelBase {
public:
    V1ListMeta();
    ~V1ListMeta() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetRContinue() const;
    bool RContinueIsSet() const;
    void UnsetrContinue();
    void SetRContinue(const std::string &value);

    int64_t GetRemainingItemCount() const;
    bool RemainingItemCountIsSet() const;
    void UnsetRemainingItemCount();
    void SetRemainingItemCount(int64_t value);

    std::string GetResourceVersion() const;
    bool ResourceVersionIsSet() const;
    void UnsetResourceVersion();
    void SetResourceVersion(const std::string &value);

    std::string GetSelfLink() const;
    bool SelfLinkIsSet() const;
    void UnSetSelfLink();
    void SetSelfLink(const std::string &value);

protected:
    std::string m_rContinue;
    bool m_rContinueIsSet;
    int64_t m_remainingItemCount;
    bool m_remainingItemCountIsSet;
    std::string m_resourceVersion;
    bool m_resourceVersionIsSet;
    std::string m_selfLink;
    bool m_selfLinkIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIST_META_H_ */
