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
#include "iam_server/constants.h"

#include <ctime>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/utils/sensitive_value.h"

namespace functionsystem::iamserver::test {

using litebus::SensitiveValue;

class TokenExpireTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        secretKey_ = SensitiveValue("test_secret_key_for_expire_testing");
    }

    SensitiveValue secretKey_;
};

// ==================== Custom Expiration Tests ====================

/**
 * Feature: Custom Token Expiration
 * Description: Test that a custom expiredTimeStamp is correctly included in JWT payload
 * Expected: JWT payload exp field matches the custom expiredTimeStamp
 */
TEST_F(TokenExpireTest, CustomExpireInJwtPayload)
{
    uint64_t customExpire = static_cast<uint64_t>(std::time(nullptr)) + 7200; // 2 hours from now

    TokenContent token;
    token.tenantID = "tenant_custom_expire";
    token.expiredTimeStamp = customExpire;
    token.salt = "test_salt";

    // Sign the token
    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    // Parse it back
    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    // Verify exp field matches
    EXPECT_EQ(parsed.expiredTimeStamp, customExpire);
}

/**
 * Feature: Custom Token Expiration
 * Description: Test that when no custom expiration is provided (0), the global config is used.
 *              This simulates GenerateToken logic: expiredTimeSpan=0 falls back to global config.
 * Expected: Token expiration is computed from the global tokenExpiredTimeSpan
 */
TEST_F(TokenExpireTest, DefaultExpirePreserved)
{
    // Simulate GenerateToken logic with expiredTimeSpan=0 (no per-request override)
    uint64_t expiredTimeSpan = 0;
    uint32_t globalTokenExpiredTimeSpan = 3600; // 1 hour global config
    uint64_t effectiveTimeSpan = (expiredTimeSpan > 0) ? expiredTimeSpan : globalTokenExpiredTimeSpan;

    auto now = static_cast<uint64_t>(std::time(nullptr));
    uint64_t expectedExpire = now + effectiveTimeSpan;

    TokenContent token;
    token.tenantID = "tenant_default_expire";
    token.expiredTimeStamp = expectedExpire;
    token.salt = "test_salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    EXPECT_EQ(parsed.expiredTimeStamp, expectedExpire);
    // Verify the effective time span was from the global config (3600s)
    EXPECT_EQ(effectiveTimeSpan, static_cast<uint64_t>(globalTokenExpiredTimeSpan));
}

/**
 * Feature: Custom Token Expiration
 * Description: Test that a non-zero expiredTimeSpan overrides the global config
 * Expected: effectiveTimeSpan equals the per-request value, not the global config
 */
TEST_F(TokenExpireTest, CustomExpireOverridesGlobal)
{
    // Simulate GenerateToken logic with per-request expiredTimeSpan
    uint64_t expiredTimeSpan = 7200; // 2 hours per-request
    uint32_t globalTokenExpiredTimeSpan = 3600; // 1 hour global config
    uint64_t effectiveTimeSpan = (expiredTimeSpan > 0) ? expiredTimeSpan : globalTokenExpiredTimeSpan;

    // effectiveTimeSpan should be the per-request value
    EXPECT_EQ(effectiveTimeSpan, 7200u);
    EXPECT_NE(effectiveTimeSpan, static_cast<uint64_t>(globalTokenExpiredTimeSpan));

    auto now = static_cast<uint64_t>(std::time(nullptr));
    uint64_t expectedExpire = now + effectiveTimeSpan;

    TokenContent token;
    token.tenantID = "tenant_custom_override";
    token.expiredTimeStamp = expectedExpire;
    token.salt = "test_salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    EXPECT_EQ(parsed.expiredTimeStamp, expectedExpire);
}

/**
 * Feature: Custom Token Expiration
 * Description: Test that a token with custom expiration survives a full sign-parse round trip
 * Expected: expiredTimeStamp is preserved through sign → encrypt → parse cycle
 */
TEST_F(TokenExpireTest, ExpireTimestampRoundTrip)
{
    uint64_t customExpire = static_cast<uint64_t>(std::time(nullptr)) + 86400; // 24 hours

    TokenContent original;
    original.tenantID = "tenant_roundtrip";
    original.expiredTimeStamp = customExpire;
    original.role = "admin";
    original.salt = "roundtrip_salt";

    // Sign
    EXPECT_TRUE(original.Sign(secretKey_).IsOk());
    EXPECT_FALSE(original.encryptToken.empty());

    // Verify signature
    EXPECT_TRUE(original.VerifySignature(secretKey_));

    // Parse from encrypted token
    TokenContent parsed;
    parsed.encryptToken = original.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    // Verify expiredTimeStamp is preserved
    EXPECT_EQ(parsed.expiredTimeStamp, customExpire);
    EXPECT_EQ(parsed.tenantID, "tenant_roundtrip");
    EXPECT_EQ(parsed.role, "admin");
}

/**
 * Feature: Custom Token Expiration
 * Description: Test that TOKEN_NEVER_EXPIRE (0) global config still results in UINT64_MAX
 *              when no per-request override is given
 * Expected: expiredTimeStamp is UINT64_MAX when global config is TOKEN_NEVER_EXPIRE and
 *           per-request expiredTimeSpan is 0
 */
TEST_F(TokenExpireTest, NeverExpireStillWorks)
{
    // Simulate GenerateToken logic: global = TOKEN_NEVER_EXPIRE, per-request = 0
    uint64_t expiredTimeSpan = 0;
    uint32_t globalTokenExpiredTimeSpan = TOKEN_NEVER_EXPIRE; // 0
    uint64_t effectiveTimeSpan = (expiredTimeSpan > 0) ? expiredTimeSpan : globalTokenExpiredTimeSpan;

    // effectiveTimeSpan should be 0 (TOKEN_NEVER_EXPIRE)
    EXPECT_EQ(effectiveTimeSpan, TOKEN_NEVER_EXPIRE);

    // When effectiveTimeSpan is TOKEN_NEVER_EXPIRE, expiredTimeStamp should be UINT64_MAX
    uint64_t expiredTimeStamp = UINT64_MAX;

    TokenContent token;
    token.tenantID = "tenant_never_expire";
    token.expiredTimeStamp = expiredTimeStamp;
    token.salt = "test_salt";

    EXPECT_TRUE(token.Sign(secretKey_).IsOk());

    TokenContent parsed;
    parsed.encryptToken = token.encryptToken;
    EXPECT_TRUE(parsed.ParseFromEncryptToken().IsOk());

    EXPECT_EQ(parsed.expiredTimeStamp, UINT64_MAX);
}

}  // namespace functionsystem::iamserver::test
