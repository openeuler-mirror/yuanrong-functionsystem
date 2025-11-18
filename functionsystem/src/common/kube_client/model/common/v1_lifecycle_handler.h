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
 * V1LifecycleHandler.h
 *
 * LifecycleHandler defines a specific action that should be taken in a lifecycle hook. One and only one of the fields,
 * except TCPSocket must be specified.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIFECYCLE_HANDLER_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LIFECYCLE_HANDLER_H_

#include "model_base.h"
#include "v1_exec_action.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_LIFECYCLE_HANDLER = "V1LifecycleHandler";

class V1LifecycleHandler : public ModelBase {
public:
    V1LifecycleHandler();
    ~V1LifecycleHandler() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1ExecAction> GetExec() const;
    bool ExecIsSet() const;
    void UnsetExec();
    void SetExec(const std::shared_ptr<V1ExecAction> &value);

protected:
    std::shared_ptr<V1ExecAction> m_exec;
    bool m_execIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1LifecycleHandler_H_ */
