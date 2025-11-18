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

#include "v1_exec_action.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ExecAction);

V1ExecAction::V1ExecAction()
{
    m_commandIsSet = false;
}

V1ExecAction::~V1ExecAction()
{
}

nlohmann::json V1ExecAction::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_commandIsSet) {
        val["command"] = ModelUtils::ToJson(m_command);
    }

    return val;
}

bool V1ExecAction::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("command")) {
        const nlohmann::json &fieldValue = val.at("command");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetCommand;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCommand);
            SetCommand(refValSetCommand);
        }
    }
    return ok;
}

std::vector<std::string> &V1ExecAction::GetCommand()
{
    return m_command;
}

void V1ExecAction::SetCommand(const std::vector<std::string> &value)
{
    m_command = value;
    m_commandIsSet = true;
}

bool V1ExecAction::CommandIsSet() const
{
    return m_commandIsSet;
}

void V1ExecAction::UnsetCommand()
{
    m_commandIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
