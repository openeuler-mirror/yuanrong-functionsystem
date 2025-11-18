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

#include "aksk_util.h"

#include <iomanip>
#include <set>

#include "common/constants/constants.h"
#include "common/hex/hex.h"
#include "common/http/http_util.h"
#include "common/proto/pb/posix_pb.h"
#include "common/utils/time_utils.h"
#include "openssl/sha.h"
#include "sign_request.h"
#include "utils/os_utils.hpp"
#include "utils/string_utils.hpp"

namespace functionsystem {
const double TIMESTAMP_EXPIRE_DURATION_SECONDS = 60;
const uint32_t BYTE_PRE_HEX = 2;

std::map<std::string, std::string> SignHttpRequest(const SignRequest &request, const KeyForAKSK &key)
{
    const std::string timestamp = litebus::time::GetCurrentUTCTime();
    unsigned char result[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(request.body_.c_str()), request.body_.size(), result);
    std::string encodedBody = CharToHexString(result, SHA256_DIGEST_LENGTH);
    std::string canonicalRequest =
        GetCanonicalRequest(request.method_, request.path_, request.queries_, request.header_, encodedBody);
    std::string signHeaders = GetSignedHeaders(request.header_);
    std::string signatureData = GenSignatureData(canonicalRequest, timestamp);
    std::stringstream ss;
    ss << "HmacSha256 ";
    ss << "timestamp=" << timestamp << ",";
    ss << "ak=" << key.GetAccessKeyId() << ",";
    ss << "signature=" << litebus::hmac::HMACAndSHA256(key.GetSecretKey(), signatureData, false);
    std::map<std::string, std::string> retHeaders = { { HEADER_SIGNED_HEADER_KEY, signHeaders },
                                                      { HEADER_TOKEN_KEY, ss.str() } };
    return retHeaders;
}

bool VerifyHttpRequest(const SignRequest &request, const KeyForAKSK &key)
{
    auto it = request.header_.find(HEADER_TOKEN_KEY);
    if (it == request.header_.end()) {
        return false;
    }
    std::string signatureToken = it->second;
    it = request.header_.find(HEADER_SIGNED_HEADER_KEY);
    if (it == request.header_.end()) {
        return false;
    }
    std::string signedHeaderKeys = it->second;

    std::string alg;
    std::string timestamp;
    std::string ak;
    std::string expectSignature;
    if (!ParseAuthToken(signatureToken, alg, timestamp, ak, expectSignature)) {
        return false;
    }
    if (alg != "HmacSha256") {
        return false;
    }

    std::set<std::string> signedHeaders;
    std::istringstream iss(signedHeaderKeys);
    std::string part;
    while (std::getline(iss, part, ';')) {
        signedHeaders.insert(part);
    }
    auto signedHeaderMap = request.header_;
    // range map， remove key not in signedHeaders set
    for (it = signedHeaderMap.begin(); it != signedHeaderMap.end();) {
        auto h = it->first;
        ToLower(h);
        if (signedHeaders.find(h) == signedHeaders.end()) {
            it = signedHeaderMap.erase(it);
        } else {
            ++it;
        }
    }

    unsigned char result[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(request.body_.c_str()), request.body_.size(), result);
    std::string encodedBody = CharToHexString(result, SHA256_DIGEST_LENGTH);

    std::string canonicalRequest =
        GetCanonicalRequest(request.method_, request.path_, request.queries_, signedHeaderMap, encodedBody);
    std::string actualSignatureData = GenSignatureData(canonicalRequest, timestamp);
    std::string actualSignature = litebus::hmac::HMACAndSHA256(key.GetSecretKey(), actualSignatureData);
    return expectSignature == actualSignature;
}

std::string SignActorMsg(const std::string &msgName, const std::string &msgBody, const KeyForAKSK &key)
{
    unsigned char result[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(msgBody.c_str()), msgBody.size(), result);
    std::string encodedBody = CharToHexString(result, SHA256_DIGEST_LENGTH);
    std::string canonicalMsg = GetActorMsgRequest(msgName, encodedBody);
    const std::string timestamp = litebus::time::GetCurrentUTCTime();
    std::string signatureData = GenSignatureData(canonicalMsg, timestamp);
    std::stringstream ss;
    ss << "HmacSha256 ";
    ss << "timestamp=" << timestamp << ",";
    ss << "ak=" << key.GetAccessKeyId() << ",";
    ss << "signature=" << litebus::hmac::HMACAndSHA256(key.GetSecretKey(), signatureData, false);
    return ss.str();
}

bool VerifyActorMsg(const std::string &msgName, const std::string &msgBody, const std::string &signatureToken,
                    const KeyForAKSK &key)
{
    std::string alg;
    std::string timestamp;
    std::string ak;
    std::string expectSignature;
    if (!ParseAuthToken(signatureToken, alg, timestamp, ak, expectSignature)) {
        return false;
    }
    if (alg != "HmacSha256") {
        return false;
    }
    unsigned char result[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(msgBody.c_str()), msgBody.size(), result);
    std::string encodedBody = CharToHexString(result, SHA256_DIGEST_LENGTH);

    std::string canonicalMsg = GetActorMsgRequest(msgName, encodedBody);
    std::string actualSignatureData = GenSignatureData(canonicalMsg, timestamp);
    std::string actualSignature = litebus::hmac::HMACAndSHA256(key.GetSecretKey(), actualSignatureData, false);
    return expectSignature == actualSignature;
}

std::string GetActorMsgRequest(const std::string &msgName, const std::string &sha256)
{
    std::stringstream ss;
    ss << msgName << "\n";
    if (sha256.empty()) {
        // default empty canonical sha256
        ss << EMPTY_CONTENT_SHA256;
    } else {
        ss << sha256;
    }
    return ss.str();
}

std::string GenSignatureData(const std::string &canonicalRequest, const std::string &timestamp)
{
    std::stringstream ss;
    ss << timestamp << " ";
    unsigned char result[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(canonicalRequest.c_str()), canonicalRequest.size(), result);
    for (const auto &c : result) {
        ss << HEX_STRING_SET[c >> FIRST_FOUR_BIT_MOVE] << HEX_STRING_SET[c & 0xf];
    }

    return ss.str();
}

bool ParseAuthToken(const std::string &str, std::string &alg, std::string &timestamp, std::string &ak,
                    std::string &signature)
{
    std::istringstream iss(str);
    std::string key;
    std::string value;
    // read alg such as: HmacSha256
    std::getline(iss, alg, ' ');
    // read timestamp
    if (std::getline(iss, key, '=') && key == "timestamp" && std::getline(iss, value, ',')) {
        timestamp = value.substr(0, value.size());  // Remove trailing comma
    } else {
        return false;
    }

    // read ak
    if (std::getline(iss, key, '=') && key == "ak" && std::getline(iss, value, ',')) {
        ak = value.substr(0, value.size());  // Remove trailing comma
    } else {
        return false;
    }

    // read signature
    if (std::getline(iss, key, '=') && key == "signature" && std::getline(iss, value, ',')) {
        signature = value.substr(0, value.size());  // Remove trailing comma
    } else {
        return false;
    }
    return true;
}

Status InitLitebusAKSKEnv(const CommonFlags &flags)
{
    if (flags.GetSystemAuthMode() != SYSTEM_AUTH_MODE_AK_SK) {
        litebus::os::SetEnv(litebus::os::LITEBUS_AKSK_ENABLED, "0");
        return Status::OK();
    }
    YRLOG_WARN("AK/SK is currently not supported.");
    return Status::OK();
}

SensitiveValue GetComponentDataKey()
{
    auto dataKeyStr = litebus::os::GetEnv(LITEBUS_DATA_KEY);
    if (dataKeyStr.IsNone()) {
        return {};
    }
    [[maybe_unused]] Raii dataKeyStrRaii([&dataKeyStr]() {
        auto &str = const_cast<std::string &>(dataKeyStr.Get());
        auto ret = memset_s(&str[0], dataKeyStr.Get().length(), 0, dataKeyStr.Get().length());
        if (ret != 0) {
            YRLOG_ERROR("key memset failed, err: {}", ret);
        }
    });

    const auto size = dataKeyStr.Get().size() / BYTE_PRE_HEX;
    char dataKey[size];
    // convert upper hex string to char string
    HexStringToCharStringCap(dataKeyStr.Get().c_str(), dataKeyStr.Get().size(), dataKey, size);
    return {dataKey, size};
}

litebus::Option<std::string> SerializeBodyToString(const runtime_rpc::StreamingMessage &message)
{
    const google::protobuf::Descriptor *descriptor = runtime_rpc::StreamingMessage::GetDescriptor();
    const google::protobuf::Reflection *reflection = runtime_rpc::StreamingMessage::GetReflection();
    const google::protobuf::OneofDescriptor *oneOfDescriptor = descriptor->FindOneofByName("body");
    const google::protobuf::FieldDescriptor *fieldDescriptor =
        reflection->GetOneofFieldDescriptor(message, oneOfDescriptor);
    if (fieldDescriptor) {
        const google::protobuf::Message &bodyField = reflection->GetMessage(message, fieldDescriptor);
        std::stringstream ss;
        litebus::hmac::SHA256AndHex(bodyField.DebugString(), ss);
        return ss.str();
    } else {
        YRLOG_ERROR("failed to get body string of message({}), empty field descriptor", message.messageid());
        return litebus::None();
    }
}

bool SignStreamingMessage(const std::string &accessKey, const SensitiveValue &secretKey,
                          const std::shared_ptr<runtime_rpc::StreamingMessage> &message)
{
    auto body = SerializeBodyToString(*message);
    if (body.IsNone()) {
        return false;
    }

    auto timestamp = litebus::time::GetCurrentUTCTime();
    std::string signKey = accessKey + ":" + timestamp + ":" + body.Get();
    auto signature = litebus::hmac::HMACAndSHA256(secretKey, signKey, false);

    (*message->mutable_metadata())["access_key"] = accessKey;
    (*message->mutable_metadata())["signature"] = signature;
    (*message->mutable_metadata())["timestamp"] = timestamp;
    return true;
}

bool VerifyStreamingMessage(const std::string &accessKey, const SensitiveValue &secretKey,
                            const std::shared_ptr<runtime_rpc::StreamingMessage> &message)
{
    ASSERT_IF_NULL(message);
    auto timestamp = message->metadata().find("timestamp");
    if (timestamp == message->metadata().end() || timestamp->second.empty()) {
        YRLOG_ERROR("failed to verify message({}), failed to find timestamp in meta-data", message->messageid());
        return false;
    }

    auto currentTimestamp = litebus::time::GetCurrentUTCTime();
    if (IsLaterThan(currentTimestamp, timestamp->second, TIMESTAMP_EXPIRE_DURATION_SECONDS)) {
        YRLOG_ERROR(
            "failed to verify message({}), failed to verify timestamp, difference is more than 1 min ({} vs {})",
            message->messageid(), currentTimestamp, timestamp->second);
        return false;
    }

    auto signature = message->metadata().find("signature");
    if (signature == message->metadata().end() || signature->second.empty()) {
        YRLOG_ERROR("failed to verify message({}), failed to find signature in meta-data", message->messageid());
        return false;
    }

    auto bodyOpt = SerializeBodyToString(*message);
    if (bodyOpt.IsNone()) {
        return false;
    }
    std::string signKey = accessKey + ":" + timestamp->second + ":" + bodyOpt.Get();
    if (litebus::hmac::HMACAndSHA256(secretKey, signKey, false) != signature->second) {
        YRLOG_ERROR("failed to verify message({}), failed to verify signature ", message->messageid());
        return false;
    }
    return true;
}

std::string SignTimestamp(const std::string &accessKey, const SensitiveValue &secretKey, const std::string &timestamp)
{
    return litebus::hmac::HMACAndSHA256(secretKey, accessKey + ":" + timestamp, false);
}

bool VerifyTimestamp(const std::string &accessKey, const SensitiveValue &secretKey, const std::string &timestamp,
                     const std::string &signature)
{
    auto currentTimestamp = litebus::time::GetCurrentUTCTime();
    if (IsLaterThan(currentTimestamp, timestamp, TIMESTAMP_EXPIRE_DURATION_SECONDS)) {
        YRLOG_ERROR("failed to verify timestamp, difference is more than 1 min ({} vs {})", currentTimestamp,
                    timestamp);
        return false;
    }

    std::string signKey = accessKey + ":" + timestamp;
    if (litebus::hmac::HMACAndSHA256(secretKey, signKey, false) != signature) {
        YRLOG_ERROR("failed to verify timestamp, signature isn't the same");
        return false;
    }
    return true;
}

std::map<std::string, std::string> SignHttpRequestX(const SignRequest &request, const KeyForAKSK &key)
{
    const std::string timestamp = std::to_string(static_cast<uint64_t>(std::time(nullptr))) + "000";
    std::string canonicalPath = request.path_.empty() ? "/" : EscapeURL(request.path_, true);
    std::string canonicalHeaders = GetCanonicalHeadersX(request.header_);
    std::string canonicalQueries = GetCanonicalQueries(request.queries_);
    std::stringstream ssR;
    ssR << request.method_ << "&" << canonicalPath << "&" << canonicalHeaders << canonicalQueries << "&"
        << request.body_ << "&"
        << "appid=" << key.GetAccessKeyId() << "&"
        << "timestamp=" << timestamp;
    std::string hex = litebus::hmac::HMACAndSHA256(key.GetSecretKey(), ssR.str(), false);
    std::string signature = BytesToBase64(HexToBytes(hex));
    std::stringstream ss;
    ss << "CLOUDSOA-HMAC-SHA256 appid=" << key.GetAccessKeyId();
    ss << ",timestamp=" << timestamp;
    ss << ",signature=" << "\"" << signature << "\"";
    std::string signedHeaders = GetSignedHeaders(request.header_);
    std::map<std::string, std::string> retHeaders = { { "x-signed-headers", signedHeaders },
                                                      { HEADER_AUTHORIZATION, ss.str() } };
    return retHeaders;
}
}  // namespace functionsystem