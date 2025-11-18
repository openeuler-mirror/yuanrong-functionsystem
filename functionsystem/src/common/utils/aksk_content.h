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

#ifndef COMMON_UTILS_AKSK_CONTENT_H
#define COMMON_UTILS_AKSK_CONTENT_H

#include <nlohmann/json.hpp>

#include "common/aksk/aksk_util.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/hex/hex.h"
#include "sensitive_value.h"

namespace functionsystem {

const std::string TENANT_ID_STR = "tenantID";
const std::string ACCESS_KEY_STR = "accessKey";
const std::string SECRET_KEY_STR = "secretKey";
const std::string DATA_KEY_STR = "dataKey";
const std::string EXPIRED_TIME_STAMP_STR = "expiredTimeStamp";
const std::string EXPIRED_TIME_SPAN_STR = "expiredTimeSpan";
const std::string CREDENTIAL_NAME_KEY_STR = "credentialName";
const std::string SERVICE_NAME_KEY_STR = "serviceName";
const std::string MICROSERVICE_NAMES_KEY_STR = "microserviceNames";
const uint32_t DATA_KEY_IV_LEN = 12;
const std::string SPLIT_SYMBOL = "_";
// if credential will be expired in $offset seconds, we need to generate new right now
const uint32_t NEW_CREDENTIAL_EXPIRED_OFFSET = 30;
// tenant roles definition
const std::string ROLE_STR = "role";
const std::string SYSTEM_ROLE = "system";
const std::string NORMAL_ROLE = "normal";
const std::string UNCONFIRMED_ROLE = "unconfirmed";

struct AKSKContent {
    std::string tenantID;
    std::string accessKey;
    SensitiveValue secretKey;
    SensitiveValue dataKey;
    uint64_t expiredTimeStamp{ 0 };
    std::string role{ UNCONFIRMED_ROLE };

    Status status{ Status::OK() };

    Status IsValid(const uint32_t offset = 0)
    {
        if (status.IsError()) {
            return status;
        }
        if (tenantID.empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "aksk tenantID is empty");
        }
        if (accessKey.empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "aksk accessKey is empty");
        }
        if (secretKey.Empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "aksk secretKey is empty");
        }
        if (dataKey.Empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "aksk dataKey is empty");
        }
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (expiredTimeStamp < now + offset && expiredTimeStamp > 0) {
            return Status(StatusCode::PARAMETER_ERROR, "aksk expired time stamp is earlier than now, expiredTimeStamp: "
                                                           + std::to_string(expiredTimeStamp));
        }
        return status;
    }

    std::shared_ptr<AKSKContent> Copy()
    {
        auto akskContent = std::make_shared<AKSKContent>();
        akskContent->tenantID = tenantID;
        akskContent->accessKey = accessKey;
        akskContent->secretKey = secretKey;
        akskContent->dataKey = dataKey;
        akskContent->expiredTimeStamp = expiredTimeStamp;
        akskContent->role = role;
        return akskContent;
    }
};

struct EncAKSKContent {
    std::string tenantID;
    std::string accessKey;
    std::string secretKey;
    std::string dataKey;
    uint64_t expiredTimeStamp;
    // only for datasystem to refresh cache in case for clock inconsistency
    uint64_t expiredTimeSpan{ 0 };
    std::string role{ UNCONFIRMED_ROLE };

    Status status{ Status::OK() };

    Status IsValid(const uint32_t offset = 0)
    {
        if (status.IsError()) {
            return status;
        }
        if (tenantID.empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "encrypt aksk tenantID is empty");
        }
        if (accessKey.empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "encrypt aksk tenantID is empty");
        }
        if (secretKey.empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "encrypt aksk secretKey is empty");
        }
        if (dataKey.empty()) {
            return Status(StatusCode::PARAMETER_ERROR, "encrypt aksk dataKey is empty");
        }
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (expiredTimeStamp < now + offset && expiredTimeStamp > 0) {
            return Status(StatusCode::PARAMETER_ERROR,
                          "encrypt aksk expired time stamp is earlier than now, expiredTimeStamp: "
                              + std::to_string(expiredTimeStamp));
        }
        return status;
    }
};

struct PermanentCredential {
    std::string tenantID;
    // if sts is enabled, will save credential to sts, get credential from sts
    std::string credentialName;
    std::string serviceName;
    std::vector<std::string> microserviceNames;
};

[[maybe_unused]] static std::shared_ptr<EncAKSKContent> TransToEncAKSKContentFromJson(const std::string &json)
{
    auto encAKSKContent = std::make_shared<EncAKSKContent>();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse encrypt aksk content, error: {}", error.what());
        encAKSKContent->status =
            Status(StatusCode::PARAMETER_ERROR, "parse json failed, err: " + std::string(error.what()));
        return encAKSKContent;
    }
    if (j.find(TENANT_ID_STR) != j.end()) {
        encAKSKContent->tenantID = j.at(TENANT_ID_STR).get<std::string>();
    }
    if (j.find(ACCESS_KEY_STR) != j.end()) {
        encAKSKContent->accessKey = j.at(ACCESS_KEY_STR).get<std::string>();
    }
    if (j.find(SECRET_KEY_STR) != j.end()) {
        encAKSKContent->secretKey = j.at(SECRET_KEY_STR).get<std::string>();
    }
    if (j.find(DATA_KEY_STR) != j.end()) {
        encAKSKContent->dataKey = j.at(DATA_KEY_STR).get<std::string>();
    }
    if (j.find(EXPIRED_TIME_STAMP_STR) != j.end()) {
        std::string expiredTimeStampStr = j.at(EXPIRED_TIME_STAMP_STR).get<std::string>();
        try {
            encAKSKContent->expiredTimeStamp = std::stoull(expiredTimeStampStr);
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse expiredTimeStamp, error: {}", e.what());
            encAKSKContent->status = Status(StatusCode::PARAMETER_ERROR,
                                            "parse expiredTimeStamp failed, err: " + std::string(e.what()));
            return encAKSKContent;
        }
    }
    if (j.find(EXPIRED_TIME_SPAN_STR) != j.end()) {
        std::string expiredTimeSpanStr = j.at(EXPIRED_TIME_SPAN_STR).get<std::string>();
        try {
            encAKSKContent->expiredTimeSpan = std::stoull(expiredTimeSpanStr);
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse expiredTimeSpan, error: {}", e.what());
            encAKSKContent->status = Status(StatusCode::PARAMETER_ERROR,
                                            "parse expiredTimeSpan failed, err: " + std::string(e.what()));
            return encAKSKContent;
        }
    }
    if (j.find(ROLE_STR) != j.end()) {
        encAKSKContent->role = j.at(ROLE_STR).get<std::string>();
    }
    return encAKSKContent;
}

[[maybe_unused]] static std::shared_ptr<AKSKContent> TransToAKSKContentFromJson(const std::string &json)
{
    auto akskContent = std::make_shared<AKSKContent>();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse plain aksk content, error: {}", error.what());
        akskContent->status =
            Status(StatusCode::PARAMETER_ERROR, "parse json failed, err: " + std::string(error.what()));
        return akskContent;
    }
    if (j.find(TENANT_ID_STR) != j.end()) {
        akskContent->tenantID = j.at(TENANT_ID_STR).get<std::string>();
    }
    if (j.find(ACCESS_KEY_STR) != j.end()) {
        akskContent->accessKey = j.at(ACCESS_KEY_STR).get<std::string>();
    }
    if (j.find(SECRET_KEY_STR) != j.end()) {
        akskContent->secretKey = HexStringToCharString(j.at(SECRET_KEY_STR).get<std::string>());
    }
    if (j.find(DATA_KEY_STR) != j.end()) {
        akskContent->dataKey = HexStringToCharString(j.at(DATA_KEY_STR).get<std::string>());
    }
    akskContent->expiredTimeStamp = 0;
    if (j.find(ROLE_STR) != j.end()) {
        akskContent->role = j.at(ROLE_STR).get<std::string>();
    }
    return akskContent;
}

[[maybe_unused]] inline std::shared_ptr<AKSKContent> TransToAKSKContentFromJsonNew(const std::string &json)
{
    auto content = std::make_shared<AKSKContent>();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse plain aksk content, error: {}", error.what());
        content->status =
            Status(StatusCode::PARAMETER_ERROR, "parse json failed, err: " + std::string(error.what()));
        return content;
    }
    if (j.find(ACCESS_KEY_STR) != j.end()) {
        content->accessKey = j.at(ACCESS_KEY_STR).get<std::string>();
    }
    if (j.find(SECRET_KEY_STR) != j.end()) {
        content->secretKey = HexStringToCharString(j.at(SECRET_KEY_STR).get<std::string>());
    }
    if (j.find(DATA_KEY_STR) != j.end()) {
        content->dataKey = HexStringToCharString(j.at(DATA_KEY_STR).get<std::string>());
    }
    return content;
}

[[maybe_unused]] static std::string TransToJsonFromAKSKContent(const std::shared_ptr<AKSKContent> &akskContent)
{
    nlohmann::json val = nlohmann::json::object();
    val[TENANT_ID_STR] = akskContent->tenantID;
    val[ACCESS_KEY_STR] = akskContent->accessKey;
    val[SECRET_KEY_STR] =
        CharStringToHexString(std::string(akskContent->secretKey.GetData(), akskContent->secretKey.GetSize()));
    val[DATA_KEY_STR] =
        CharStringToHexString(std::string(akskContent->dataKey.GetData(), akskContent->dataKey.GetSize()));
    return val.dump();
}

[[maybe_unused]] static std::string TransToJsonFromEncAKSKContent(const std::shared_ptr<EncAKSKContent> &encAKSKContent)
{
    nlohmann::json val = nlohmann::json::object();
    val[TENANT_ID_STR] = encAKSKContent->tenantID;
    val[ACCESS_KEY_STR] = encAKSKContent->accessKey;
    val[SECRET_KEY_STR] = encAKSKContent->secretKey;
    val[DATA_KEY_STR] = encAKSKContent->dataKey;
    val[EXPIRED_TIME_STAMP_STR] = std::to_string(encAKSKContent->expiredTimeStamp);
    val[EXPIRED_TIME_SPAN_STR] = std::to_string(encAKSKContent->expiredTimeSpan);
    val[ROLE_STR] = encAKSKContent->role;
    return val.dump();
}

[[maybe_unused]] static std::vector<std::shared_ptr<PermanentCredential>> TransToPermanentCredFromJson(
    const std::string &confJson)
{
    if (confJson.empty()) {
        return {};
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(confJson);
    } catch (std::exception &error) {
        YRLOG_ERROR("parse json failed, error: {}", error.what());
        return {};
    }
    if (!json.is_array()) {
        YRLOG_ERROR("config is not an array");
        return {};
    }
    std::vector<std::shared_ptr<PermanentCredential>> permanentCreds{};
    for (auto &j : json) {
        auto credential = std::make_shared<PermanentCredential>();
        if (j.find(TENANT_ID_STR) != j.end()) {
            credential->tenantID = std::move(j.at(TENANT_ID_STR).get<std::string>());
        }
        if (credential->tenantID.empty()) {
            YRLOG_WARN("tenantID is empty, skip it");
            continue;
        }
        if (j.find(CREDENTIAL_NAME_KEY_STR) != j.end()) {
            credential->credentialName = std::move(j.at(CREDENTIAL_NAME_KEY_STR).get<std::string>());
        }
        if (j.find(SERVICE_NAME_KEY_STR) != j.end()) {
            credential->serviceName = std::move(j.at(SERVICE_NAME_KEY_STR).get<std::string>());
        }
        if (j.find(MICROSERVICE_NAMES_KEY_STR) != j.end()) {
            for (const auto &microserviceName : j[MICROSERVICE_NAMES_KEY_STR]) {
                credential->microserviceNames.push_back(microserviceName);
            }
        }
        permanentCreds.emplace_back(credential);
    }
    return permanentCreds;
}

// decrypt aksk with root key & work key
[[maybe_unused]] static  std::shared_ptr<AKSKContent> DecryptAKSKContentFromStorage(
    const std::shared_ptr<EncAKSKContent> &encAKSKContent)
{
    auto akskContent = std::make_shared<AKSKContent>();
    akskContent->tenantID = encAKSKContent->tenantID;
    akskContent->accessKey = encAKSKContent->accessKey;
    akskContent->expiredTimeStamp = encAKSKContent->expiredTimeStamp;
    akskContent->role = encAKSKContent->role;
    akskContent->dataKey = encAKSKContent->dataKey;
    akskContent->secretKey = encAKSKContent->secretKey;
    return akskContent;
}

// decrypt aksk with root key & work key
[[maybe_unused]] static  std::shared_ptr<EncAKSKContent> EncryptAKSKContentForStorage(
    const std::shared_ptr<AKSKContent> &akskContent)
{
    auto encAKSKContent = std::make_shared<EncAKSKContent>();
    encAKSKContent->tenantID = akskContent->tenantID;
    encAKSKContent->accessKey = akskContent->accessKey;
    encAKSKContent->expiredTimeStamp = akskContent->expiredTimeStamp;
    encAKSKContent->role = akskContent->role;
    encAKSKContent->dataKey = akskContent->dataKey.GetData();
    encAKSKContent->secretKey = akskContent->secretKey.GetData();
    return encAKSKContent;
}

} // namespace functionsystem
#endif // COMMON_UTILS_AKSK_CONTENT_H
