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

#ifndef IAM_SERVER_INTERNAL_IAM_IAM_TOKEN_CONTENT_H
#define IAM_SERVER_INTERNAL_IAM_IAM_TOKEN_CONTENT_H

#include <nlohmann/json.hpp>

#include "common/hex/hex.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/token_transfer.h"
#include "securec.h"
#include "utils/string_utils.hpp"

namespace functionsystem::iamserver {

const int INTERNAL_IAM_TOKEN_MAX_SIZE = 4096;
const std::string JWT_SEPARATOR = ".";
const std::string JWT_HEADER = R"({"alg":"HS256","typ":"JWT"})";

struct TokenContent {
    std::string tenantID;
    uint64_t expiredTimeStamp{ 0 };
    std::string salt;
    std::string role;  // role field for JWT token
    int64_t cpuLimit{ -1 };
    std::string memLimit;
    // JWT token format: base64url(header).base64url(payload).base64url(signature)
    std::string encryptToken;

    ~TokenContent()
    {
        CleanSensitiveStrMemory(encryptToken, "deconstructing token, tenantID: " + tenantID);
    }

    enum class SerializeType {
        TENANT_ID = 0,
        EXPIRED_TIME_STAMP = 1,
        SERIALIZE_TYPE_MAX = 2,
    };

    Status IsValid(const uint32_t offset = 0) const
    {
        if (tenantID.empty()) {
            return Status(StatusCode::FAILED, "token tenantID is empty");
        }
        if (!IsJwtFormat() && salt.empty()) {
            return Status(StatusCode::FAILED, "token salt is empty");
        }
        if (encryptToken.empty()) {
            return Status(StatusCode::FAILED, "token value is empty");
        }
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (expiredTimeStamp != UINT64_MAX && expiredTimeStamp < now + offset) {
            return Status(StatusCode::FAILED, "token expired time stamp is earlier than now, expiredTimeStamp: "
                                                  + std::to_string(expiredTimeStamp));
        }
        return Status::OK();
    }

    Status Serialize(char *token, size_t &size) const
    {
        std::string splitSymbol = "__";
        auto timeStampStr = std::to_string(expiredTimeStamp);
        size = tenantID.size() + splitSymbol.size() + timeStampStr.size();
        if (size >= static_cast<size_t>(INTERNAL_IAM_TOKEN_MAX_SIZE)) {
            return Status(StatusCode::FAILED, "token size too long");
        }
        int len = sprintf_s(token, INTERNAL_IAM_TOKEN_MAX_SIZE, "%s__%s", tenantID.c_str(), timeStampStr.c_str());
        if (size != static_cast<size_t>(len)) {
            return Status(StatusCode::FAILED, "sprintf_s failed");
        }
        return Status::OK();
    }

    Status Parse(const char *token)
    {
        tenantID.clear();
        std::string timeStampStr;
        int index = 0;
        int type = static_cast<int>(SerializeType::TENANT_ID);
        while (index <= INTERNAL_IAM_TOKEN_MAX_SIZE && *(token + index) != '\0') {
            char curChar = *(token + index);
            char nextChar = *(token + index + 1);
            if (curChar == '_' && nextChar == '_') {
                // token format: xxx__xxx, has double underscore to split
                index++;
                index++;
                type++;
                continue;
            }
            if (static_cast<int>(type) >= static_cast<int>(SerializeType::SERIALIZE_TYPE_MAX)) {
                return Status(StatusCode::FAILED, "token format error");
            }
            switch (type) {
                case static_cast<int>(SerializeType::TENANT_ID):
                    tenantID += curChar;
                    break;
                case static_cast<int>(SerializeType::EXPIRED_TIME_STAMP):
                    timeStampStr += curChar;
                    break;
                default:
                    break;
            }
            index++;
        }
        if (index > INTERNAL_IAM_TOKEN_MAX_SIZE) {
            return Status(StatusCode::FAILED, "token length error");
        }
        try {
            expiredTimeStamp = std::stoull(timeStampStr);
        } catch (std::exception &e) {
            return Status(StatusCode::FAILED, "transform time stamp type failed, err:" + std::string(e.what()));
        }
        return Status::OK();
    }

    bool operator==(const TokenContent &targetToken) const
    {
        return targetToken.tenantID == tenantID && targetToken.expiredTimeStamp == expiredTimeStamp;
    }

    bool operator!=(const TokenContent &targetToken) const
    {
        return targetToken.tenantID != tenantID || targetToken.expiredTimeStamp != expiredTimeStamp;
    }

    std::shared_ptr<TokenContent> Copy()
    {
        auto tokenContent = std::make_shared<TokenContent>();
        tokenContent->salt = salt;
        tokenContent->tenantID = tenantID;
        tokenContent->expiredTimeStamp = expiredTimeStamp;
        tokenContent->role = role;
        tokenContent->cpuLimit = cpuLimit;
        tokenContent->memLimit = memLimit;
        tokenContent->encryptToken = encryptToken;
        return tokenContent;
    }

    /**
     * Generate JWT payload JSON string using nlohmann::json
     * Format: {"sub":"tenantID","exp":expiredTimeStamp,"role":"role","cpu_limit":cpuLimit,"mem_limit":"memLimit"}
     */
    std::string GetJwtPayloadJson() const
    {
        nlohmann::json payload;
        payload["sub"] = tenantID;
        payload["exp"] = (expiredTimeStamp == UINT64_MAX) ? nlohmann::json(-1)
                                                          : nlohmann::json(static_cast<int64_t>(expiredTimeStamp));
        if (!role.empty()) {
            payload["role"] = role;
        }
        if (cpuLimit != -1) {
            payload["cpu_limit"] = cpuLimit;
        }
        if (!memLimit.empty()) {
            payload["mem_limit"] = memLimit;
        }
        return payload.dump();
    }

    /**
     * Parse JWT payload JSON string using nlohmann::json
     * Format: {"sub":"tenantID","exp":expiredTimeStamp,"role":"role","cpu_limit":cpuLimit,"mem_limit":"memLimit"}
     */
    Status ParseJwtPayloadJson(const std::string &payloadJson)
    {
        try {
            nlohmann::json payload = nlohmann::json::parse(payloadJson);
            if (!payload.contains("sub") || !payload["sub"].is_string()) {
                return Status(StatusCode::FAILED, "JWT payload missing or invalid 'sub' field");
            }
            if (!payload.contains("exp") || !payload["exp"].is_number()) {
                return Status(StatusCode::FAILED, "JWT payload missing or invalid 'exp' field");
            }
            tenantID = payload["sub"].get<std::string>();
            int64_t exp = payload["exp"].get<int64_t>();
            if (exp == -1) {
                expiredTimeStamp = UINT64_MAX;
            } else if (exp < 0) {
                return Status(StatusCode::FAILED, "JWT payload contains unsupported negative 'exp' field");
            } else {
                expiredTimeStamp = static_cast<uint64_t>(exp);
            }
            // Parse optional fields
            role.clear();
            if (payload.contains("role") && payload["role"].is_string()) {
                role = payload["role"].get<std::string>();
            }
            if (payload.contains("cpu_limit") && payload["cpu_limit"].is_number()) {
                cpuLimit = payload["cpu_limit"].get<int64_t>();
            }
            if (payload.contains("mem_limit") && payload["mem_limit"].is_string()) {
                memLimit = payload["mem_limit"].get<std::string>();
            }
            return Status::OK();
        } catch (const nlohmann::json::exception &e) {
            return Status(StatusCode::FAILED, "JWT payload parse failed: " + std::string(e.what()));
        }
    }

    /**
     * Sign the token and generate JWT format
     * JWT format: base64url(header).base64url(payload).base64url(signature)
     * The result is stored in encryptToken
     * @param secretKey: the secret key for signing (HMAC-SHA256)
     * @return Status::OK() if success
     */
    Status Sign(const litebus::SensitiveValue &secretKey)
    {
        if (tenantID.empty()) {
            return Status(StatusCode::FAILED, "tenantID is empty, cannot sign");
        }

        // 1. Encode header
        std::string headerBase64 = functionsystem::Base64UrlEncodeString(JWT_HEADER);

        // 2. Encode payload
        std::string payloadJson = GetJwtPayloadJson();
        std::string payloadBase64 = functionsystem::Base64UrlEncodeString(payloadJson);

        // 3. Create signing input: header.payload
        std::string signingInput = headerBase64 + JWT_SEPARATOR + payloadBase64;

        // 4. Generate signature using HMAC-SHA256
        std::string signatureHex = litebus::hmac::HMACAndSHA256(secretKey, signingInput, false);
        if (signatureHex.empty()) {
            return Status(StatusCode::FAILED, "failed to generate HMAC signature");
        }

        // 5. Base64URL encode the signature (hex string directly for simplicity)
        std::string signatureBase64 = functionsystem::Base64UrlEncodeString(signatureHex);

        // 6. Combine to form JWT: header.payload.signature
        encryptToken = signingInput + JWT_SEPARATOR + signatureBase64;
        return Status::OK();
    }

    /**
     * Verify JWT signature using HMAC-SHA256
     * @param secretKey: the secret key for verification
     * @return true if signature is valid
     */
    bool VerifySignature(const litebus::SensitiveValue &secretKey) const
    {
        // Split JWT into parts
        auto firstDot = encryptToken.find(JWT_SEPARATOR);
        if (firstDot == std::string::npos) {
            YRLOG_ERROR("JWT format error: first separator not found");
            return false;
        }
        auto secondDot = encryptToken.find(JWT_SEPARATOR, firstDot + 1);
        if (secondDot == std::string::npos) {
            YRLOG_ERROR("JWT format error: second separator not found");
            return false;
        }

        std::string signingInput = encryptToken.substr(0, secondDot);
        std::string signatureBase64 = encryptToken.substr(secondDot + 1);

        // Decode stored signature
        std::string storedSignatureHex = functionsystem::Base64UrlDecode(signatureBase64);

        // Compute expected signature
        std::string expectedSignatureHex = litebus::hmac::HMACAndSHA256(secretKey, signingInput, false);

        return storedSignatureHex == expectedSignatureHex;
    }

    /**
     * Check if the token is in JWT format
     * @return true if encryptToken contains two JWT separators
     */
    bool IsJwtFormat() const
    {
        auto firstDot = encryptToken.find(JWT_SEPARATOR);
        if (firstDot == std::string::npos) {
            return false;
        }
        auto secondDot = encryptToken.find(JWT_SEPARATOR, firstDot + 1);
        return secondDot != std::string::npos;
    }

    /**
     * Parse JWT token from encryptToken field
     * Extracts tenantID and expiredTimeStamp from payload
     * @return Status::OK() if parsing succeeded
     */
    Status ParseJwt()
    {
        // Split JWT into parts: header.payload.signature
        auto firstDot = encryptToken.find(JWT_SEPARATOR);
        if (firstDot == std::string::npos) {
            return Status(StatusCode::FAILED, "JWT format error: first separator not found");
        }
        auto secondDot = encryptToken.find(JWT_SEPARATOR, firstDot + 1);
        if (secondDot == std::string::npos) {
            return Status(StatusCode::FAILED, "JWT format error: second separator not found");
        }

        // Extract and decode payload
        std::string payloadBase64 = encryptToken.substr(firstDot + 1, secondDot - firstDot - 1);
        std::string payloadJson = functionsystem::Base64UrlDecode(payloadBase64);

        // Parse payload JSON
        return ParseJwtPayloadJson(payloadJson);
    }

    /**
     * Parse token from encryptToken field (supports both JWT and legacy format)
     * JWT format: base64url(header).base64url(payload).base64url(signature)
     * Legacy format: tenantID__expiredTimeStamp
     */
    Status ParseFromEncryptToken()
    {
        if (IsJwtFormat()) {
            return ParseJwt();
        }
        // Fallback to legacy format
        return Parse(encryptToken.c_str());
    }

    /**
     * Check if the token has a signature (JWT format or old signed format)
     */
    bool HasSignature() const
    {
        return IsJwtFormat();
    }
};
}  // namespace functionsystem::iamserver
#endif  // IAM_SERVER_INTERNAL_IAM_IAM_TOKEN_CONTENT_H
