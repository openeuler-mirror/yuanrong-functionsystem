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

#include "iam_server/iam/internal_iam/token_content.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/utils/sensitive_value.h"

namespace functionsystem::iamserver::test {

class TokenContentTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        secretKey_ = litebus::SensitiveValue("test_secret_key_for_jwt_signing");
    }

    litebus::SensitiveValue secretKey_;
};

// ==================== JWT Payload JSON Tests ====================

TEST_F(TokenContentTest, GetJwtPayloadJsonBasic)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;

    std::string payload = token.GetJwtPayloadJson();

    // Verify JSON structure using nlohmann::json
    nlohmann::json j = nlohmann::json::parse(payload);
    EXPECT_EQ(j["sub"].get<std::string>(), "tenant123");
    EXPECT_EQ(j["exp"].get<uint64_t>(), 1737849600);
}

TEST_F(TokenContentTest, GetJwtPayloadJsonWithRole)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.role = "admin";

    std::string payload = token.GetJwtPayloadJson();

    nlohmann::json j = nlohmann::json::parse(payload);
    EXPECT_EQ(j["sub"].get<std::string>(), "tenant123");
    EXPECT_EQ(j["exp"].get<uint64_t>(), 1737849600);
    EXPECT_EQ(j["role"].get<std::string>(), "admin");
}

TEST_F(TokenContentTest, GetJwtPayloadJsonWithoutRole)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.role = "";  // Empty role

    std::string payload = token.GetJwtPayloadJson();

    nlohmann::json j = nlohmann::json::parse(payload);
    EXPECT_EQ(j["sub"].get<std::string>(), "tenant123");
    EXPECT_EQ(j["exp"].get<uint64_t>(), 1737849600);
    EXPECT_FALSE(j.contains("role"));  // Should not have role field when empty
}

TEST_F(TokenContentTest, GetJwtPayloadJsonEmptyTenantID)
{
    TokenContent token;
    token.tenantID = "";
    token.expiredTimeStamp = 1737849600;

    std::string payload = token.GetJwtPayloadJson();

    nlohmann::json j = nlohmann::json::parse(payload);
    EXPECT_EQ(j["sub"].get<std::string>(), "");
    EXPECT_EQ(j["exp"].get<uint64_t>(), 1737849600);
}

TEST_F(TokenContentTest, GetJwtPayloadJsonMaxTimestamp)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.expiredTimeStamp = UINT64_MAX;  // Never expire token

    std::string payload = token.GetJwtPayloadJson();

    nlohmann::json j = nlohmann::json::parse(payload);
    EXPECT_EQ(j["exp"].get<uint64_t>(), UINT64_MAX);
}

TEST_F(TokenContentTest, GetJwtPayloadJsonPermanentTokenUsesMinusOne)
{
    TokenContent token;
    token.tenantID = "tenant_permanent";
    token.expiredTimeStamp = UINT64_MAX;

    std::string payload = token.GetJwtPayloadJson();

    EXPECT_THAT(payload, ::testing::HasSubstr(R"("exp":-1)"));
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonBasic)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant456","exp":1737936000})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(token.tenantID, "tenant456");
    EXPECT_EQ(token.expiredTimeStamp, 1737936000);
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonWithRole)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant456","exp":1737936000,"role":"operator"})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(token.tenantID, "tenant456");
    EXPECT_EQ(token.expiredTimeStamp, 1737936000);
    EXPECT_EQ(token.role, "operator");
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonPermanentTokenMinusOne)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant456","exp":-1,"role":"operator"})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(token.tenantID, "tenant456");
    EXPECT_EQ(token.expiredTimeStamp, UINT64_MAX);
    EXPECT_EQ(token.role, "operator");
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonWithoutRole)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant456","exp":1737936000})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(token.tenantID, "tenant456");
    EXPECT_EQ(token.expiredTimeStamp, 1737936000);
    EXPECT_TRUE(token.role.empty());  // Role should be empty if not present
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonMissingSub)
{
    TokenContent token;
    std::string payload = R"({"exp":1737936000})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("sub"));
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonMissingExp)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant"})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("exp"));
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonInvalidSubType)
{
    TokenContent token;
    std::string payload = R"({"sub":123,"exp":1737936000})";  // sub should be string

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("sub"));
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonInvalidExpType)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant","exp":"invalid"})";  // exp should be number

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("exp"));
}

TEST_F(TokenContentTest, ParseJwtPayloadJsonInvalidJson)
{
    TokenContent token;
    std::string payload = "not a json";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("parse failed"));
}

TEST_F(TokenContentTest, JwtPayloadJsonRoundTrip)
{
    TokenContent original;
    original.tenantID = "test_tenant_roundtrip";
    original.expiredTimeStamp = 1737849600;

    std::string payload = original.GetJwtPayloadJson();

    TokenContent parsed;
    Status status = parsed.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(parsed.tenantID, original.tenantID);
    EXPECT_EQ(parsed.expiredTimeStamp, original.expiredTimeStamp);
}

TEST_F(TokenContentTest, JwtPayloadJsonRoundTripWithRole)
{
    TokenContent original;
    original.tenantID = "test_tenant_roundtrip";
    original.expiredTimeStamp = 1737849600;
    original.role = "admin";

    std::string payload = original.GetJwtPayloadJson();

    TokenContent parsed;
    Status status = parsed.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(parsed.tenantID, original.tenantID);
    EXPECT_EQ(parsed.expiredTimeStamp, original.expiredTimeStamp);
    EXPECT_EQ(parsed.role, original.role);
}

// ==================== JWT Sign Tests ====================

TEST_F(TokenContentTest, SignBasic)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.salt = "test_salt";

    Status status = token.Sign(secretKey_);

    EXPECT_TRUE(status.IsOk());
    EXPECT_FALSE(token.encryptToken.empty());
}

TEST_F(TokenContentTest, SignProducesJwtFormat)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.salt = "test_salt";

    Status status = token.Sign(secretKey_);

    EXPECT_TRUE(status.IsOk());
    // JWT format: header.payload.signature (3 parts separated by .)
    auto firstDot = token.encryptToken.find(".");
    EXPECT_NE(firstDot, std::string::npos);
    auto secondDot = token.encryptToken.find(".", firstDot + 1);
    EXPECT_NE(secondDot, std::string::npos);
    // No third dot
    auto thirdDot = token.encryptToken.find(".", secondDot + 1);
    EXPECT_EQ(thirdDot, std::string::npos);
}

TEST_F(TokenContentTest, SignEmptyTenantIDFails)
{
    TokenContent token;
    token.tenantID = "";
    token.expiredTimeStamp = 1737849600;
    token.salt = "test_salt";

    Status status = token.Sign(secretKey_);

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("tenantID is empty"));
}

TEST_F(TokenContentTest, SignDeterministic)
{
    TokenContent token1;
    token1.tenantID = "tenant123";
    token1.expiredTimeStamp = 1737849600;
    token1.salt = "test_salt";

    TokenContent token2;
    token2.tenantID = "tenant123";
    token2.expiredTimeStamp = 1737849600;
    token2.salt = "test_salt";

    EXPECT_TRUE(token1.Sign(secretKey_).IsOk());
    EXPECT_TRUE(token2.Sign(secretKey_).IsOk());

    // Same inputs should produce same JWT
    EXPECT_EQ(token1.encryptToken, token2.encryptToken);
}

TEST_F(TokenContentTest, SignDifferentKeyProducesDifferentToken)
{
    TokenContent token1;
    token1.tenantID = "tenant123";
    token1.expiredTimeStamp = 1737849600;
    token1.salt = "test_salt";

    TokenContent token2;
    token2.tenantID = "tenant123";
    token2.expiredTimeStamp = 1737849600;
    token2.salt = "test_salt";

    SensitiveValue key1("secret_key_1");
    SensitiveValue key2("secret_key_2");

    EXPECT_TRUE(token1.Sign(key1).IsOk());
    EXPECT_TRUE(token2.Sign(key2).IsOk());

    // Different keys should produce different JWTs
    EXPECT_NE(token1.encryptToken, token2.encryptToken);
}

// ==================== JWT Verify Signature Tests ====================

TEST_F(TokenContentTest, VerifySignatureValid)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.salt = "test_salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    bool isValid = token.VerifySignature(secretKey_);
    EXPECT_TRUE(isValid);
}

TEST_F(TokenContentTest, VerifySignatureWrongKey)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.salt = "test_salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    SensitiveValue wrongKey("wrong_secret_key");
    bool isValid = token.VerifySignature(wrongKey);
    EXPECT_FALSE(isValid);
}

TEST_F(TokenContentTest, VerifySignatureTamperedPayload)
{
    TokenContent token;
    token.tenantID = "tenant123";
    token.expiredTimeStamp = 1737849600;
    token.salt = "test_salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    // Tamper with the token (change a character in the payload)
    auto firstDot = token.encryptToken.find(".");
    auto secondDot = token.encryptToken.find(".", firstDot + 1);
    if (firstDot != std::string::npos && secondDot != std::string::npos) {
        size_t payloadPos = firstDot + 1;
        if (payloadPos < token.encryptToken.size()) {
            token.encryptToken[payloadPos] = 'X';  // Tamper
        }
    }

    bool isValid = token.VerifySignature(secretKey_);
    EXPECT_FALSE(isValid);
}

TEST_F(TokenContentTest, VerifySignatureInvalidFormat)
{
    TokenContent token;
    token.encryptToken = "invalid_format_without_dots";

    bool isValid = token.VerifySignature(secretKey_);
    EXPECT_FALSE(isValid);
}

TEST_F(TokenContentTest, VerifySignatureOnlyOneDot)
{
    TokenContent token;
    token.encryptToken = "header.payload";  // Missing signature

    bool isValid = token.VerifySignature(secretKey_);
    EXPECT_FALSE(isValid);
}

// ==================== IsJwtFormat Tests ====================

TEST_F(TokenContentTest, IsJwtFormatTrue)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.expiredTimeStamp = 1737849600;
    token.salt = "salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());
    EXPECT_TRUE(token.IsJwtFormat());
}

TEST_F(TokenContentTest, IsJwtFormatFalseNoDots)
{
    TokenContent token;
    token.encryptToken = "legacytokenformat";

    EXPECT_FALSE(token.IsJwtFormat());
}

TEST_F(TokenContentTest, IsJwtFormatFalseOneDot)
{
    TokenContent token;
    token.encryptToken = "header.payload";

    EXPECT_FALSE(token.IsJwtFormat());
}

TEST_F(TokenContentTest, IsJwtFormatFalseLegacy)
{
    TokenContent token;
    token.encryptToken = "tenant123__1737849600";

    EXPECT_FALSE(token.IsJwtFormat());
}

// ==================== ParseJwt Tests ====================

TEST_F(TokenContentTest, ParseJwtBasic)
{
    // First sign a token
    TokenContent original;
    original.tenantID = "tenant_parse_test";
    original.expiredTimeStamp = 1737849600;
    original.salt = "test_salt";
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());

    // Create new token with just the encryptToken
    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;

    Status status = parsed.ParseJwt();

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(parsed.tenantID, "tenant_parse_test");
    EXPECT_EQ(parsed.expiredTimeStamp, 1737849600);
}

TEST_F(TokenContentTest, ParseJwtWithRole)
{
    // First sign a token with role
    TokenContent original;
    original.tenantID = "tenant_parse_test";
    original.expiredTimeStamp = 1737849600;
    original.role = "operator";
    original.salt = "test_salt";
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());

    // Create new token with just the encryptToken
    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;

    Status status = parsed.ParseJwt();

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(parsed.tenantID, "tenant_parse_test");
    EXPECT_EQ(parsed.expiredTimeStamp, 1737849600);
    EXPECT_EQ(parsed.role, "operator");
}

TEST_F(TokenContentTest, ParseJwtInvalidFormat)
{
    TokenContent token;
    token.encryptToken = "invalid_format";

    Status status = token.ParseJwt();

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("separator"));
}

TEST_F(TokenContentTest, ParseJwtMissingSignature)
{
    TokenContent token;
    token.encryptToken = "header.payload";

    Status status = token.ParseJwt();

    EXPECT_FALSE(status.IsOk());
}

// ==================== ParseFromEncryptToken Tests ====================

TEST_F(TokenContentTest, ParseFromEncryptTokenJwtFormat)
{
    TokenContent original;
    original.tenantID = "jwt_tenant";
    original.expiredTimeStamp = 1737849600;
    original.salt = "test_salt";
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;

    Status status = parsed.ParseFromEncryptToken();

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(parsed.tenantID, "jwt_tenant");
    EXPECT_EQ(parsed.expiredTimeStamp, 1737849600);
}

TEST_F(TokenContentTest, ParseFromEncryptTokenLegacyFormat)
{
    TokenContent token;
    token.encryptToken = "legacy_tenant__1737849600";

    Status status = token.ParseFromEncryptToken();

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(token.tenantID, "legacy_tenant");
    EXPECT_EQ(token.expiredTimeStamp, 1737849600);
}

// ==================== HasSignature Tests ====================

TEST_F(TokenContentTest, HasSignatureTrue)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.expiredTimeStamp = 1737849600;
    token.salt = "salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());
    EXPECT_TRUE(token.HasSignature());
}

TEST_F(TokenContentTest, HasSignatureFalse)
{
    TokenContent token;
    token.encryptToken = "tenant__1737849600";

    EXPECT_FALSE(token.HasSignature());
}

// ==================== IsValid Tests ====================

TEST_F(TokenContentTest, IsValidWithNeverExpire)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.salt = "salt";
    token.encryptToken = "token";
    token.expiredTimeStamp = UINT64_MAX;  // Never expire

    Status status = token.IsValid();

    EXPECT_TRUE(status.IsOk());
}

TEST_F(TokenContentTest, IsValidEmptyTenantID)
{
    TokenContent token;
    token.tenantID = "";
    token.salt = "salt";
    token.encryptToken = "token";
    token.expiredTimeStamp = UINT64_MAX;

    Status status = token.IsValid();

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("tenantID is empty"));
}

TEST_F(TokenContentTest, IsValidEmptySalt)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.salt = "";
    token.encryptToken = "token";
    token.expiredTimeStamp = UINT64_MAX;

    Status status = token.IsValid();

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("salt is empty"));
}

TEST_F(TokenContentTest, IsValidEmptyEncryptToken)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.salt = "salt";
    token.encryptToken = "";
    token.expiredTimeStamp = UINT64_MAX;

    Status status = token.IsValid();

    EXPECT_FALSE(status.IsOk());
    EXPECT_THAT(status.ToString(), ::testing::HasSubstr("token value is empty"));
}

// ==================== Serialize/Parse Tests ====================

TEST_F(TokenContentTest, SerializeParseRoundTrip)
{
    TokenContent original;
    original.tenantID = "test_tenant";
    original.expiredTimeStamp = 1737849600;

    char buffer[INTERNAL_IAM_TOKEN_MAX_SIZE];
    size_t size = 0;
    EXPECT_TRUE(original.Serialize(buffer, size).IsOk());

    TokenContent parsed;
    EXPECT_TRUE(parsed.Parse(buffer).IsOk());

    EXPECT_EQ(parsed.tenantID, original.tenantID);
    EXPECT_EQ(parsed.expiredTimeStamp, original.expiredTimeStamp);
}

// ==================== Copy Tests ====================

TEST_F(TokenContentTest, CopyCreatesDeepCopy)
{
    TokenContent original;
    original.tenantID = "tenant";
    original.expiredTimeStamp = 1737849600;
    original.salt = "salt";
    original.role = "admin";
    original.encryptToken = "token";

    auto copy = original.Copy();

    EXPECT_EQ(copy->tenantID, original.tenantID);
    EXPECT_EQ(copy->expiredTimeStamp, original.expiredTimeStamp);
    EXPECT_EQ(copy->salt, original.salt);
    EXPECT_EQ(copy->role, original.role);
    EXPECT_EQ(copy->encryptToken, original.encryptToken);

    // Modify original, copy should not change
    original.tenantID = "modified";
    original.role = "modified_role";
    EXPECT_EQ(copy->tenantID, "tenant");
    EXPECT_EQ(copy->role, "admin");
}

// ==================== Equality Operator Tests ====================

TEST_F(TokenContentTest, EqualityOperatorTrue)
{
    TokenContent token1;
    token1.tenantID = "tenant";
    token1.expiredTimeStamp = 1737849600;

    TokenContent token2;
    token2.tenantID = "tenant";
    token2.expiredTimeStamp = 1737849600;

    EXPECT_TRUE(token1 == token2);
    EXPECT_FALSE(token1 != token2);
}

TEST_F(TokenContentTest, EqualityOperatorFalseDifferentTenantID)
{
    TokenContent token1;
    token1.tenantID = "tenant1";
    token1.expiredTimeStamp = 1737849600;

    TokenContent token2;
    token2.tenantID = "tenant2";
    token2.expiredTimeStamp = 1737849600;

    EXPECT_FALSE(token1 == token2);
    EXPECT_TRUE(token1 != token2);
}

TEST_F(TokenContentTest, EqualityOperatorFalseDifferentTimestamp)
{
    TokenContent token1;
    token1.tenantID = "tenant";
    token1.expiredTimeStamp = 1737849600;

    TokenContent token2;
    token2.tenantID = "tenant";
    token2.expiredTimeStamp = 1737936000;

    EXPECT_FALSE(token1 == token2);
    EXPECT_TRUE(token1 != token2);
}

// ==================== Full JWT Flow Tests ====================

TEST_F(TokenContentTest, FullJwtFlowSignAndVerify)
{
    // Create and sign token
    TokenContent original;
    original.tenantID = "production_tenant";
    original.expiredTimeStamp = 1737849600;
    original.salt = "random_salt";
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());

    // Verify signature
    EXPECT_TRUE(original.VerifySignature(secretKey_));

    // Parse back from JWT
    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    // Verify parsed data matches original
    EXPECT_EQ(parsed.tenantID, original.tenantID);
    EXPECT_EQ(parsed.expiredTimeStamp, original.expiredTimeStamp);
}

TEST_F(TokenContentTest, FullJwtFlowWithNeverExpire)
{
    TokenContent token;
    token.tenantID = "never_expire_tenant";
    token.expiredTimeStamp = UINT64_MAX;  // Never expire
    token.salt = "salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());
    EXPECT_TRUE(token.VerifySignature(secretKey_));
    EXPECT_TRUE(token.IsJwtFormat());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());
    EXPECT_EQ(parsed.expiredTimeStamp, UINT64_MAX);
}

TEST_F(TokenContentTest, FullJwtFlowWithRole)
{
    // Create and sign token with role
    TokenContent original;
    original.tenantID = "production_tenant";
    original.expiredTimeStamp = 1737849600;
    original.role = "superadmin";
    original.salt = "random_salt";
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());

    // Verify signature
    EXPECT_TRUE(original.VerifySignature(secretKey_));

    // Parse back from JWT
    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    // Verify parsed data matches original including role
    EXPECT_EQ(parsed.tenantID, original.tenantID);
    EXPECT_EQ(parsed.expiredTimeStamp, original.expiredTimeStamp);
    EXPECT_EQ(parsed.role, original.role);
}

}  // namespace functionsystem::iamserver::test
