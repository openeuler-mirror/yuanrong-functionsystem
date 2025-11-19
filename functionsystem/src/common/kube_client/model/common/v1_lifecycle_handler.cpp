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

#include "v1_lifecycle_handler.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1LifecycleHandler);

V1LifecycleHandler::V1LifecycleHandler()
{
    m_execIsSet = false;
}

V1LifecycleHandler::~V1LifecycleHandler()
{
}

nlohmann::json V1LifecycleHandler::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_execIsSet) {
        val["exec"] = ModelUtils::ToJson(m_exec);
    }

    return val;
}

bool V1LifecycleHandler::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("exec")) {
        const nlohmann::json &fieldValue = val.at("exec");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ExecAction> refValSetExec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetExec);
            SetExec(refValSetExec);
        }
    }
    return ok;
}

std::shared_ptr<V1ExecAction> V1LifecycleHandler::GetExec() const
{
    return m_exec;
}

void V1LifecycleHandler::SetExec(const std::shared_ptr<V1ExecAction> &value)
{
    m_exec = value;
    m_execIsSet = true;
}

bool V1LifecycleHandler::ExecIsSet() const
{
    return m_execIsSet;
}

void V1LifecycleHandler::UnsetExec()
{
    m_execIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
