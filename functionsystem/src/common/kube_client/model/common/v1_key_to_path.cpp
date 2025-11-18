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

#include "v1_key_to_path.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1KeyToPath);

V1KeyToPath::V1KeyToPath()
{
    m_key = std::string("");
    m_keyIsSet = false;
    m_mode = 0;
    m_modeIsSet = false;
    m_path = std::string("");
    m_pathIsSet = false;
}

V1KeyToPath::~V1KeyToPath()
{
}

nlohmann::json V1KeyToPath::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_keyIsSet) {
        val["key"] = ModelUtils::ToJson(m_key);
    }
    if (m_modeIsSet) {
        val["mode"] = ModelUtils::ToJson(m_mode);
    }
    if (m_pathIsSet) {
        val["path"] = ModelUtils::ToJson(m_path);
    }

    return val;
}

bool V1KeyToPath::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("key")) {
        const nlohmann::json &fieldValue = val.at("key");
        if (!fieldValue.is_null()) {
            std::string refValSetKey;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKey);
            SetKey(refValSetKey);
        }
    }
    if (val.contains("mode")) {
        const nlohmann::json &fieldValue = val.at("mode");
        if (!fieldValue.is_null()) {
            int64_t refValSetMode;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMode);
            SetMode(refValSetMode);
        }
    }
    if (val.contains("path")) {
        const nlohmann::json &fieldValue = val.at("path");
        if (!fieldValue.is_null()) {
            std::string refValSetPath;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPath);
            SetPath(refValSetPath);
        }
    }
    return ok;
}

std::string V1KeyToPath::GetKey() const
{
    return m_key;
}

void V1KeyToPath::SetKey(const std::string &value)
{
    m_key = value;
    m_keyIsSet = true;
}

bool V1KeyToPath::KeyIsSet() const
{
    return m_keyIsSet;
}

void V1KeyToPath::UnsetKey()
{
    m_keyIsSet = false;
}
int64_t V1KeyToPath::GetMode() const
{
    return m_mode;
}

void V1KeyToPath::SetMode(int64_t value)
{
    m_mode = value;
    m_modeIsSet = true;
}

bool V1KeyToPath::ModeIsSet() const
{
    return m_modeIsSet;
}

void V1KeyToPath::UnsetMode()
{
    m_modeIsSet = false;
}
std::string V1KeyToPath::GetPath() const
{
    return m_path;
}

void V1KeyToPath::SetPath(const std::string &value)
{
    m_path = value;
    m_pathIsSet = true;
}

bool V1KeyToPath::PathIsSet() const
{
    return m_pathIsSet;
}

void V1KeyToPath::UnsetPath()
{
    m_pathIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
