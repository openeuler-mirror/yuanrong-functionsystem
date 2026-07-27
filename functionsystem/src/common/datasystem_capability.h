/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_COMMON_DATASYSTEM_CAPABILITY_H
#define FUNCTIONSYSTEM_COMMON_DATASYSTEM_CAPABILITY_H

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace functionsystem::datasystem_capability {

inline constexpr char YR_DATASYSTEM_DEPLOYED[] = "YR_DATASYSTEM_DEPLOYED";
inline constexpr char YR_BYPASS_DATASYSTEM[] = "YR_BYPASS_DATASYSTEM";

struct Capability {
    bool dataSystemDeployed{ true };
    bool bypassDataSystem{ false };

    bool IsValid() const
    {
        return dataSystemDeployed || bypassDataSystem;
    }
};

inline bool ParseBoolean(std::string value, bool defaultValue)
{
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return defaultValue;
}

inline bool GetEnvironmentBoolean(const std::string &name, bool defaultValue)
{
    const char *value = std::getenv(name.c_str());
    return value == nullptr ? defaultValue : ParseBoolean(value, defaultValue);
}

template <typename EnvMap>
bool ResolveBoolean(const EnvMap &envs, const std::string &name, bool defaultValue)
{
    auto iter = envs.find(name);
    return iter == envs.end() ? GetEnvironmentBoolean(name, defaultValue)
                              : ParseBoolean(iter->second, GetEnvironmentBoolean(name, defaultValue));
}

template <typename EnvMap>
Capability ResolveCapability(const EnvMap &envs)
{
    return {
        ResolveBoolean(envs, YR_DATASYSTEM_DEPLOYED, true),
        ResolveBoolean(envs, YR_BYPASS_DATASYSTEM, false),
    };
}

inline Capability GetEnvironmentCapability()
{
    return {
        GetEnvironmentBoolean(YR_DATASYSTEM_DEPLOYED, true),
        GetEnvironmentBoolean(YR_BYPASS_DATASYSTEM, false),
    };
}

inline const char *BooleanString(bool value)
{
    return value ? "true" : "false";
}

}  // namespace functionsystem::datasystem_capability

#endif  // FUNCTIONSYSTEM_COMMON_DATASYSTEM_CAPABILITY_H
