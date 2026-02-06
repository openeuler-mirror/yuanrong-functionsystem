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

namespace functionsystem::iamserver::test {

using litebus::SensitiveValue;

class IAMActorRoleTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        secretKey_ = SensitiveValue("test_secret_key_for_role_testing");
    }

    SensitiveValue secretKey_;
};

// ==================== Role Field Tests ====================

/**
 * Feature: TokenContent Role Field Support
 * Description: Test that role field is correctly included in JWT payload
 * Expected: JWT payload contains role field when role is set
 */
TEST_F(IAMActorRoleTest, JwtPayloadContainsRoleWhenSet)
{
    TokenContent token;
    token.tenantID = "tenant_with_role";
    token.expiredTimeStamp = 1737849600;
    token.role = "admin";

    std::string payload = token.GetJwtPayloadJson();
    nlohmann::json j = nlohmann::json::parse(payload);

    EXPECT_TRUE(j.contains("role"));
    EXPECT_EQ(j["role"].get<std::string>(), "admin");
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test that role field is excluded from JWT payload when empty
 * Expected: JWT payload does not contain role field when role is empty
 */
TEST_F(IAMActorRoleTest, JwtPayloadExcludesRoleWhenEmpty)
{
    TokenContent token;
    token.tenantID = "tenant_without_role";
    token.expiredTimeStamp = 1737849600;
    token.role = "";  // Empty role

    std::string payload = token.GetJwtPayloadJson();
    nlohmann::json j = nlohmann::json::parse(payload);

    EXPECT_FALSE(j.contains("role"));
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test various role values
 * Expected: Different role values are correctly encoded
 */
TEST_F(IAMActorRoleTest, JwtPayloadSupportsVariousRoles)
{
    std::vector<std::string> roles = {"admin", "operator", "viewer", "superadmin", "guest"};

    for (const auto& role : roles) {
        TokenContent token;
        token.tenantID = "tenant";
        token.expiredTimeStamp = 1737849600;
        token.role = role;

        std::string payload = token.GetJwtPayloadJson();
        nlohmann::json j = nlohmann::json::parse(payload);

        EXPECT_EQ(j["role"].get<std::string>(), role);
    }
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test role parsing from JWT payload
 * Expected: Role is correctly parsed from JWT payload
 */
TEST_F(IAMActorRoleTest, ParseRoleFromJwtPayload)
{
    TokenContent token;
    std::string payload = R"({"sub":"tenant","exp":1737849600,"role":"operator"})";

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(token.role, "operator");
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test role parsing when not present in JWT payload
 * Expected: Role is empty when not present in payload
 */
TEST_F(IAMActorRoleTest, ParseRoleNotPresentInPayload)
{
    TokenContent token;
    token.role = "default_role";  // Set a default
    std::string payload = R"({"sub":"tenant","exp":1737849600})";  // No role field

    Status status = token.ParseJwtPayloadJson(payload);

    EXPECT_TRUE(status.IsOk());
    EXPECT_TRUE(token.role.empty());  // Should be empty when not in payload
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test full JWT flow with role (sign, verify, parse)
 * Expected: Role is preserved through complete JWT lifecycle
 */
TEST_F(IAMActorRoleTest, FullJwtFlowPreservesRole)
{
    // Create token with role
    TokenContent original;
    original.tenantID = "flow_test_tenant";
    original.expiredTimeStamp = 1737849600;
    original.role = "manager";
    original.salt = "test_salt";

    // Sign
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());

    // Verify signature
    EXPECT_TRUE(original.VerifySignature(secretKey_));

    // Parse
    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    // Verify role is preserved
    EXPECT_EQ(parsed.role, "manager");
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test Copy method preserves role field
 * Expected: Copied token contains same role value
 */
TEST_F(IAMActorRoleTest, CopyPreservesRole)
{
    TokenContent original;
    original.tenantID = "tenant";
    original.expiredTimeStamp = 1737849600;
    original.role = "admin";
    original.salt = "salt";
    original.encryptToken = "token";

    auto copy = original.Copy();

    EXPECT_EQ(copy->role, original.role);

    // Modify original role
    original.role = "modified";
    EXPECT_EQ(copy->role, "admin");  // Copy should not change
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test role with special characters
 * Expected: Role with special characters is correctly encoded/decoded
 */
TEST_F(IAMActorRoleTest, RoleWithSpecialCharacters)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.expiredTimeStamp = 1737849600;
    token.role = "admin:read-write";  // Role with special chars
    token.salt = "salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    EXPECT_EQ(parsed.role, "admin:read-write");
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test multiple tokens with different roles
 * Expected: Each token maintains its own role value
 */
TEST_F(IAMActorRoleTest, MultipleTokensWithDifferentRoles)
{
    std::vector<std::pair<std::string, std::string>> tenantRoles = {
        {"tenant1", "admin"},
        {"tenant2", "operator"},
        {"tenant3", "viewer"}
    };

    std::vector<TokenContent> tokens;
    for (const auto& [tenant, role] : tenantRoles) {
        TokenContent token;
        token.tenantID = tenant;
        token.expiredTimeStamp = 1737849600;
        token.role = role;
        token.salt = "salt";
        EXPECT_TRUE(token.Sign(secretKey_).IsOk());
        tokens.push_back(token);
    }

    // Verify each token has correct role
    for (size_t i = 0; i < tokens.size(); ++i) {
        TokenContent parsed;
        parsed.encryptToken = tokens[i].encryptToken;
        EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());
        EXPECT_EQ(parsed.role, tenantRoles[i].second);
    }
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test long role string
 * Expected: Long role strings are correctly handled
 */
TEST_F(IAMActorRoleTest, LongRoleString)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.expiredTimeStamp = 1737849600;
    token.role = std::string(256, 'x');  // 256 character role
    token.salt = "salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    EXPECT_EQ(parsed.role.length(), 256);
    EXPECT_EQ(parsed.role, token.role);
}

/**
 * Feature: TokenContent Role Field Support
 * Description: Test role with unicode characters
 * Expected: Unicode roles are correctly handled
 */
TEST_F(IAMActorRoleTest, RoleWithUnicode)
{
    TokenContent token;
    token.tenantID = "tenant";
    token.expiredTimeStamp = 1737849600;
    token.role = "管理员";  // Chinese characters
    token.salt = "salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    EXPECT_EQ(parsed.role, "管理员");
}

}  // namespace functionsystem::iamserver::test
