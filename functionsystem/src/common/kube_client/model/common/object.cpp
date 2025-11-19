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

#include "object.h"

namespace functionsystem::kube_client {
namespace model {

Object::Object()
{
    m_object = nlohmann::json::object();
}

Object::~Object()
{
}

nlohmann::json Object::ToJson() const
{
    return m_object;
}

bool Object::FromJson(const nlohmann::json &val)
{
    m_object = val;
    m_isSet = true;
    return IsSet();
}

nlohmann::json Object::GetValue(const std::string &key) const
{
    return m_object.at(key);
}

void Object::SetValue(const std::string &key, const nlohmann::json &value)
{
    if (!value.is_null()) {
        m_object[key] = value;
        m_isSet = true;
    }
}

}  // namespace model
}  // namespace functionsystem::kube_client
