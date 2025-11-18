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

#include "v1_lifecycle.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Lifecycle);

V1Lifecycle::V1Lifecycle()
{
    m_postStartIsSet = false;
    m_preStopIsSet = false;
}

V1Lifecycle::~V1Lifecycle()
{
}

nlohmann::json V1Lifecycle::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_postStartIsSet) {
        val["postStart"] = ModelUtils::ToJson(m_postStart);
    }
    if (m_preStopIsSet) {
        val["preStop"] = ModelUtils::ToJson(m_preStop);
    }

    return val;
}

bool V1Lifecycle::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("postStart")) {
        const nlohmann::json &fieldValue = val.at("postStart");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1LifecycleHandler> refValSetPostStart;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPostStart);
            SetPostStart(refValSetPostStart);
        }
    }
    if (val.contains("preStop")) {
        const nlohmann::json &fieldValue = val.at("preStop");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1LifecycleHandler> refValSetPreStop;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPreStop);
            SetPreStop(refValSetPreStop);
        }
    }
    return ok;
}

std::shared_ptr<V1LifecycleHandler> V1Lifecycle::GetPostStart() const
{
    return m_postStart;
}

void V1Lifecycle::SetPostStart(const std::shared_ptr<V1LifecycleHandler> &value)
{
    m_postStart = value;
    m_postStartIsSet = true;
}

bool V1Lifecycle::PostStartIsSet() const
{
    return m_postStartIsSet;
}

void V1Lifecycle::UnsetPostStart()
{
    m_postStartIsSet = false;
}
std::shared_ptr<V1LifecycleHandler> V1Lifecycle::GetPreStop() const
{
    return m_preStop;
}

void V1Lifecycle::SetPreStop(const std::shared_ptr<V1LifecycleHandler> &value)
{
    m_preStop = value;
    m_preStopIsSet = true;
}

bool V1Lifecycle::PreStopIsSet() const
{
    return m_preStopIsSet;
}

void V1Lifecycle::UnsetPreStop()
{
    m_preStopIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
