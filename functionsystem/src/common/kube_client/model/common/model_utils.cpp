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

#include "model_utils.h"

namespace functionsystem::kube_client {
namespace model {

ModelUtils::ModelUtils()
{
}
ModelUtils::~ModelUtils()
{
}
nlohmann::json ModelUtils::ToJson(bool val)
{
    return nlohmann::json(val);
}
nlohmann::json ModelUtils::ToJson(float val)
{
    return nlohmann::json(val);
}
nlohmann::json ModelUtils::ToJson(double val)
{
    return nlohmann::json(val);
}
nlohmann::json ModelUtils::ToJson(int32_t val)
{
    return nlohmann::json(val);
}
nlohmann::json ModelUtils::ToJson(int64_t val)
{
    return nlohmann::json(val);
}
nlohmann::json ModelUtils::ToJson(const std::string &val)
{
    return nlohmann::json(val);
}
nlohmann::json ModelUtils::ToJson(const utility::Datetime &val)
{
    return nlohmann::json(val.ToString());
}
nlohmann::json ModelUtils::ToJson(const nlohmann::json &val)
{
    return val;
}

nlohmann::json ModelUtils::ToJson(const std::shared_ptr<utility::Datetime> &val)
{
    nlohmann::json retVal;
    if (val != nullptr) {
        retVal = ToJson(*val);
    }
    return retVal;
}
bool ModelUtils::FromJson(const nlohmann::json &val, bool &outVal)
{
    outVal = !val.is_boolean() ? false : val.get<bool>();
    return val.is_boolean();
}
bool ModelUtils::FromJson(const nlohmann::json &val, float &outVal)
{
    outVal = (!val.is_number_float() && !val.is_number_integer()) ? std::numeric_limits<float>::quiet_NaN()
                                                                  : val.get<float>();
    return val.is_number_float() || val.is_number_integer();
}
bool ModelUtils::FromJson(const nlohmann::json &val, double &outVal)
{
    outVal = (!val.is_number_float() && !val.is_number_integer()) ? std::numeric_limits<double>::quiet_NaN()
                                                                  : val.get<double>();
    return val.is_number_float() || val.is_number_integer();
}
bool ModelUtils::FromJson(const nlohmann::json &val, int32_t &outVal)
{
    outVal = !val.is_number_integer() ? std::numeric_limits<int32_t>::quiet_NaN() : val.get<int32_t>();
    return val.is_number_integer();
}
bool ModelUtils::FromJson(const nlohmann::json &val, int64_t &outVal)
{
    outVal = !val.is_number() ? std::numeric_limits<int64_t>::quiet_NaN() : val.get<int64_t>();
    return val.is_number();
}
bool ModelUtils::FromJson(const nlohmann::json &val, std::string &outVal)
{
    outVal = val.is_string() ? val.get<std::string>() : std::string("");
    return val.is_string();
}
bool ModelUtils::FromJson(const nlohmann::json &val, utility::Datetime &outVal)
{
    outVal = val.is_null() ? utility::Datetime::FromString(std::string("NULL"))
                           : utility::Datetime::FromString(val.get<std::string>());
    return outVal.IsInitialized();
}
bool ModelUtils::FromJson(const nlohmann::json &val, nlohmann::json &outVal)
{
    outVal = val;
    return !val.is_null();
}

bool ModelUtils::FromJson(const nlohmann::json &val, std::shared_ptr<utility::Datetime> &outVal)
{
    bool ok = false;
    if (outVal == nullptr) {
        outVal = std::make_shared<utility::Datetime>();
    }
    if (outVal != nullptr) {
        ok = FromJson(val, *outVal);
    }
    return ok;
}

bool ModelUtils::CheckHasKey(const nlohmann::json &val, const std::string key)
{
    if (val.contains(key)) {
        const nlohmann::json &fieldValue = val.at(key);
        if (!fieldValue.is_null()) {
            return true;
        }
    }
    return false;
}

}  // namespace model
}  // namespace functionsystem::kube_client