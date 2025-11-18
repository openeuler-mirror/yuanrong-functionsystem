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
#ifndef COMMON_UTILS_META_STORAGE_KEY_OPERATION_H
#define COMMON_UTILS_META_STORAGE_KEY_OPERATION_H

#include <nlohmann/json.hpp>
#include <string>
#include <utils/string_utils.hpp>

#include "common/metadata/metadata.h"

namespace functionsystem {
const std::string INSTANCE_PATH_PREFIX = "/sn/instance/business/yrk/tenant";
const std::string GROUP_PATH_PREFIX = "/yr/group";
const std::string INSTANCE_ROUTE_PATH_PREFIX = "/yr/route/business/yrk";
const std::string BUSPROXY_PATH_PREFIX = "/yr/busproxy/business/yrk/tenant";
const std::string FUNC_META_PATH_PREFIX = "/yr/functions/business/yrk/tenant";
const std::string POD_POOL_PREFIX = "/yr/podpools/info";
const std::string INTERNAL_IAM_TOKEN_PREFIX = "/yr/iam/token";
const std::string INTERNAL_IAM_AKSK_PREFIX = "/yr/iam/aksk";
const std::string DEBUG_INSTANCE_PREFIX = "/yr/debug/";
const std::string NEW_PREFIX = "/new";
const std::string OLD_PREFIX = "/old";
const uint32_t INSTANCE_INFO_KEY_LEN = 14;
const uint32_t ROUTE_INFO_KEY_LEN = 6;

struct InstanceKeyInfo {
    std::string instanceID;
    std::string requestID;
};


inline std::string TrimKeyPrefix(const std::string &key, const std::string prefix = "")
{
    if (prefix.empty()) {
        return key;
    }
    if (litebus::strings::StartsWithPrefix(key, prefix)) {
        return key.substr(prefix.length());
    }
    return key;
}

static std::string GetKeyLastItem(const std::string &key, const std::string &sep, const uint32_t &len)
{
    auto keyItems = litebus::strings::Split(key, sep, len);
    if (keyItems.size() < len) {
        return "";
    }
    return *keyItems.rbegin();
}

inline std::string GetIPFromAddress(const std::string &address)
{
    static const uint32_t LEN = 2;
    auto keyItems = litebus::strings::Split(address, ":", LEN);
    if (keyItems.size() < LEN) {
        return "";
    }
    return *keyItems.begin();
}

inline InstanceKeyInfo ParseInstanceKey(const std::string &instanceKey)
{
    static const uint32_t META_INSTANCE_ID_INDEX = 13;
    static const uint32_t META_REQUEST_ID_INDEX = 12;
    static const uint32_t ROUTE_INSTANCE_ID_INDEX = 5;
    static const uint32_t INSTANCE_INFO_PREFIX_INDEX = 2;
    auto keyInfo = InstanceKeyInfo{};
    auto keyItems = litebus::strings::Split(instanceKey, "/");
    if (keyItems.size() == INSTANCE_INFO_KEY_LEN && keyItems[1] == "sn"
        && keyItems[INSTANCE_INFO_PREFIX_INDEX] == "instance") {
        keyInfo.instanceID = keyItems[META_INSTANCE_ID_INDEX];
        keyInfo.requestID = keyItems[META_REQUEST_ID_INDEX];
        return keyInfo;
    }

    if (keyItems.size() == ROUTE_INFO_KEY_LEN) {
        keyInfo.instanceID = keyItems[ROUTE_INSTANCE_ID_INDEX];
        return keyInfo;
    }
    return keyInfo;
}

[[maybe_unused]] static std::string GetInstanceID(const std::string &eventKey)
{
    return GetKeyLastItem(eventKey, "/", INSTANCE_INFO_KEY_LEN);
}

[[maybe_unused]] static std::string GetPodPoolID(const std::string &eventKey)
{
    static const uint32_t POD_POOL_KEY_LEN = 5;
    return GetKeyLastItem(eventKey, "/", POD_POOL_KEY_LEN);
}

[[maybe_unused]] static std::string GetProxyNode(const std::string &proxyKey)
{
    static const uint32_t PROXY_KEY_LEN = 9;
    return GetKeyLastItem(proxyKey, "/", PROXY_KEY_LEN);
}

[[maybe_unused]] static ProxyMeta GetProxyMeta(const std::string &jsonStr)
{
    ProxyMeta proxyMeta{};
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse proxy meta, error: {}", error.what());
        return proxyMeta;
    }
    if (j.find("node") != j.end()) {
        j.at("node").get_to(proxyMeta.node);
    }
    if (j.find("aid") != j.end()) {
        j.at("aid").get_to(proxyMeta.aid);
    }
    if (j.find("ak") != j.end()) {
        j.at("ak").get_to(proxyMeta.ak);
    }
    return proxyMeta;
}

[[maybe_unused]] static litebus::Option<std::string> GetLastFunctionNameFromKey(const std::string &functionKey)
{
    // need to modify format of instance key when control plane is ready
    // funcKey format: 12345678901234561234567890123456/0-test-helloWorld/$latest
    static const std::string SEP = "/";
    static const int32_t FUNC_KEY_LEN = 3;
    static const int32_t FUNC_NAME_LEN = 3;
    static const int32_t FUNCTION_POSITION = 1;
    static const int32_t FUNCTION_NAME_POSITION = 2;
    YRLOG_DEBUG("gen instance key from function({})", functionKey);
    auto items = litebus::strings::Split(functionKey, SEP);
    if (items.size() != FUNC_KEY_LEN) {
        YRLOG_WARN("len of items is {}, not equal to func key length: {}", items.size(), FUNC_KEY_LEN);
        return litebus::None();
    }
    auto nameItems = litebus::strings::Split(items[FUNCTION_POSITION], "-");
    if (nameItems.size() != FUNC_NAME_LEN) {
        YRLOG_WARN("len of items is {}, not equal to func name length: {}", nameItems.size(), FUNC_NAME_LEN);
        return litebus::None();
    }
    return nameItems[FUNCTION_NAME_POSITION];
}

[[maybe_unused]] static litebus::Option<std::string> GenPodPoolKey(const std::string &poolID)
{
    return POD_POOL_PREFIX + "/" + poolID;
}

[[maybe_unused]] static litebus::Option<std::string> GenInstanceKey(const std::string &functionKey,
                                                                    const std::string &instanceID,
                                                                    const std::string &requestID)
{
    // need to modify format of instance key when control plane is ready
    // funcKey format: 12345678901234561234567890123456/0-test-helloWorld/$latest
    static const std::string SEP = "/";
    static const int32_t FUNC_KEY_LEN = 3;
    static const int32_t TENANT_POSITION = 0;
    static const int32_t FUNCTION_POSITION = 1;
    static const int32_t VERSION_POSITION = 2;
    YRLOG_DEBUG("gen instance key from function({})", functionKey);
    auto items = litebus::strings::Split(functionKey, SEP);
    if (items.size() != FUNC_KEY_LEN) {
        YRLOG_WARN("len of items is {}, not equal to func key length: {}", items.size(), FUNC_KEY_LEN);
        return litebus::None();
    }
    return INSTANCE_PATH_PREFIX + "/" + items[TENANT_POSITION] + "/function/" + items[FUNCTION_POSITION] + "/version/" +
           items[VERSION_POSITION] + "/defaultaz/" + requestID + "/" + instanceID;
}

[[maybe_unused]] static std::string GenInstanceRouteKey(const std::string &instanceID)
{
    return INSTANCE_ROUTE_PATH_PREFIX + "/" + instanceID;
}

[[maybe_unused]] static std::string GenTokenKey(const std::string &clusterID, const std::string &tenantID, bool isNew)
{
    if (isNew) {
        return INTERNAL_IAM_TOKEN_PREFIX + NEW_PREFIX + "/" + clusterID + "/" + tenantID;
    }
    return INTERNAL_IAM_TOKEN_PREFIX + OLD_PREFIX + "/" + clusterID + "/" + tenantID;
}

[[maybe_unused]] static std::string GenTokenKeyWatchPrefix(const std::string &clusterID, bool isNew)
{
    if (isNew) {
        return INTERNAL_IAM_TOKEN_PREFIX + NEW_PREFIX + "/" + clusterID;
    }
    return INTERNAL_IAM_TOKEN_PREFIX + OLD_PREFIX + "/" + clusterID;
}

[[maybe_unused]] static std::string GenAKSKKey(const std::string &clusterID, const std::string &tenantID, bool isNew)
{
    if (isNew) {
        return INTERNAL_IAM_AKSK_PREFIX + NEW_PREFIX + "/" + clusterID + "/" + tenantID;
    }
    return INTERNAL_IAM_AKSK_PREFIX + OLD_PREFIX + "/" + clusterID + "/" + tenantID;
}

[[maybe_unused]] static std::string GenAKSKKeyWatchPrefix(const std::string &clusterID, bool isNew)
{
    if (isNew) {
        return INTERNAL_IAM_AKSK_PREFIX + NEW_PREFIX + "/" + clusterID;
    }
    return INTERNAL_IAM_AKSK_PREFIX + OLD_PREFIX + "/" + clusterID;
}

[[maybe_unused]] static std::string GetFuncKeyFromInstancePath(const std::string &key)
{
    static const std::string SEP = "/";
    static const int32_t ITEMS_LEN = 14;
    static const int32_t TENANT_POSITION = 6;
    static const int32_t FUNCTION_NAME_POSITION = 8;
    static const int32_t VERSION_POSITION = 10;

    auto items = litebus::strings::Split(key, SEP);
    if (items.size() != ITEMS_LEN) {
        YRLOG_WARN("len of items is {}, not equal to {}", items.size(), ITEMS_LEN);
        return "";
    }
    return items[TENANT_POSITION] + "/" + items[FUNCTION_NAME_POSITION] + "/" + items[VERSION_POSITION];
}

[[maybe_unused]] static std::string GetFuncKeyFromFuncMetaPath(const std::string &path)
{
    static const std::string SEP = "/";
    static const int32_t ITEMS_LEN = 11;
    static const int32_t TENANT_POSITION = 6;
    static const int32_t FUNCTION_NAME_POSITION = 8;
    static const int32_t VERSION_POSITION = 10;

    auto items = litebus::strings::Split(path, SEP);
    if (items.size() != ITEMS_LEN) {
        YRLOG_WARN("len of items is {}, not equal to {}", items.size(), ITEMS_LEN);
        return "";
    }
    return items[TENANT_POSITION] + "/" + items[FUNCTION_NAME_POSITION] + "/" + items[VERSION_POSITION];
}

[[maybe_unused]] static std::string GenEtcdFullFuncKey(const std::string &key)
{
    static const std::string SEP = "/";
    static const int32_t ITEMS_LEN = 3;
    static const int32_t TENANT_POSITION = 0;
    static const int32_t FUNCTION_NAME_POSITION = 1;
    static const int32_t VERSION_POSITION = 2;
    auto items = litebus::strings::Split(key, SEP);
    if (items.size() != ITEMS_LEN) {
        YRLOG_WARN("len of items is {}, not equal to {}", items.size(), ITEMS_LEN);
        return "";
    }
    return FUNC_META_PATH_PREFIX + SEP + items[TENANT_POSITION] + "/function/" + items[FUNCTION_NAME_POSITION] +
           "/version/" + items[VERSION_POSITION];
}

[[maybe_unused]] static std::string GetTokenTenantID(const std::string &key)
{
    static const uint32_t TOKEN_TENANT_ID_LENGTH = 7;
    return GetKeyLastItem(key, "/", TOKEN_TENANT_ID_LENGTH);
}

[[maybe_unused]] static std::string GetAKSKTenantID(const std::string &key)
{
    static const uint32_t AKSK_TENANT_ID_LENGTH = 7;
    return GetKeyLastItem(key, "/", AKSK_TENANT_ID_LENGTH);
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_META_STORAGE_KEY_OPERATION_H