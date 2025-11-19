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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIFECYCLE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIFECYCLE_H_

#include "model_base.h"
#include "v1_lifecycle_handler.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_LIFECYCLE = "V1Lifecycle";

class V1Lifecycle : public ModelBase {
public:
    V1Lifecycle();
    ~V1Lifecycle() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1LifecycleHandler> GetPostStart() const;
    bool PostStartIsSet() const;
    void UnsetPostStart();
    void SetPostStart(const std::shared_ptr<V1LifecycleHandler> &value);

    std::shared_ptr<V1LifecycleHandler> GetPreStop() const;
    bool PreStopIsSet() const;
    void UnsetPreStop();
    void SetPreStop(const std::shared_ptr<V1LifecycleHandler> &value);

protected:
    std::shared_ptr<V1LifecycleHandler> m_postStart;
    bool m_postStartIsSet;
    std::shared_ptr<V1LifecycleHandler> m_preStop;
    bool m_preStopIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIFECYCLE_H_ */
