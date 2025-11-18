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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_MODEL_UTILS_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_MODEL_UTILS_H

#include <map>
#include <vector>

#include "nlohmann/json.hpp"

#include "common/kube_client/api/api_exception.h"

namespace functionsystem::kube_client {
namespace model {

class ModelUtils {
public:
    ModelUtils();
    virtual ~ModelUtils();

    static nlohmann::json ToJson(bool val);
    static nlohmann::json ToJson(float val);
    static nlohmann::json ToJson(double val);
    static nlohmann::json ToJson(int32_t val);
    static nlohmann::json ToJson(int64_t val);
    static nlohmann::json ToJson(const std::string &val);
    static nlohmann::json ToJson(const utility::Datetime &val);
    static nlohmann::json ToJson(const nlohmann::json &val);
    template <typename T>
    static nlohmann::json ToJson(const std::shared_ptr<T> &val);
    static nlohmann::json ToJson(const std::shared_ptr<utility::Datetime> &val);
    template <typename T>
    static nlohmann::json ToJson(const std::vector<T> &val);
    template <typename T>
    static nlohmann::json ToJson(const std::map<std::string, T> &val);

    static bool FromJson(const nlohmann::json &val, bool &outVal);
    static bool FromJson(const nlohmann::json &val, float &outVal);
    static bool FromJson(const nlohmann::json &val, double &outVal);
    static bool FromJson(const nlohmann::json &val, int32_t &outVal);
    static bool FromJson(const nlohmann::json &val, int64_t &outVal);
    static bool FromJson(const nlohmann::json &val, std::string &outVal);
    static bool FromJson(const nlohmann::json &val, utility::Datetime &outVal);
    static bool FromJson(const nlohmann::json &val, nlohmann::json &outVal);
    template <typename T>
    static bool FromJson(const nlohmann::json &val, std::shared_ptr<T> &outVal);
    static bool FromJson(const nlohmann::json &val, std::shared_ptr<utility::Datetime> &outVal);
    template <typename T>
    static bool FromJson(const nlohmann::json &val, std::vector<T> &outVal);
    template <typename T>
    static bool FromJson(const nlohmann::json &val, std::map<std::string, T> &outVal);

    static bool CheckHasKey(const nlohmann::json &val, const std::string key);
};
template <typename T>
nlohmann::json ModelUtils::ToJson(const std::shared_ptr<T> &val)
{
    nlohmann::json retVal;
    if (val != nullptr) {
        retVal = val->ToJson();
    }
    return retVal;
}
template <typename T>
nlohmann::json ModelUtils::ToJson(const std::vector<T> &val)
{
    std::vector<nlohmann::json> ret;
    for (const auto &x : val) {
        ret.push_back(ToJson(x));
    }
    return nlohmann::json(ret);
}
template <typename T>
nlohmann::json ModelUtils::ToJson(const std::map<std::string, T> &val)
{
    nlohmann::json obj;
    for (const auto &itemkey : val) {
        obj[itemkey.first] = ToJson(itemkey.second);
    }
    return obj;
}
template <typename T>
bool ModelUtils::FromJson(const nlohmann::json &val, std::shared_ptr<T> &outVal)
{
    bool ok = false;
    if (outVal == nullptr) {
        outVal = std::make_shared<T>();
    }
    if (outVal != nullptr) {
        ok = outVal->FromJson(val);
    }
    return ok;
}
template <typename T>
bool ModelUtils::FromJson(const nlohmann::json &val, std::vector<T> &outVal)
{
    if (!val.is_array()) {
        return false;
    }
    bool ok = true;
    for (const auto jitem : val) {
        T item;
        ok &= FromJson(jitem, item);
        outVal.push_back(item);
    }
    return ok;
}
template <typename T>
bool ModelUtils::FromJson(const nlohmann::json &val, std::map<std::string, T> &outVal)
{
    bool ok = true;
    if (val.is_object()) {
        for (auto objItr = val.begin(); objItr != val.end(); objItr++) {
            T itemVal;
            ok &= FromJson(objItr.value(), itemVal);
            outVal.insert(std::pair<std::string, T>(objItr.key(), itemVal));
        }
    } else {
        ok = false;
    }
    return ok;
}

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_MODEL_UTILS_H */
