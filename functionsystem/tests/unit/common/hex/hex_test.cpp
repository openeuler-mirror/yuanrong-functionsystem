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

#include "common/hex/hex.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace functionsystem::test {

class HexTest : public ::testing::Test {};

// ==================== Base64 Encode/Decode Tests ====================

TEST_F(HexTest, Base64EncodeEmptyString)
{
    std::string input = "";
    std::string result = Base64Encode(input);
    EXPECT_EQ(result, "");
}

TEST_F(HexTest, Base64EncodeSimpleString)
{
    std::string input = "hello";
    std::string result = Base64Encode(input);
    EXPECT_EQ(result, "aGVsbG8=");
}

TEST_F(HexTest, Base64EncodeWithSpecialChars)
{
    std::string input = "hello world!";
    std::string result = Base64Encode(input);
    EXPECT_EQ(result, "aGVsbG8gd29ybGQh");
}

TEST_F(HexTest, Base64DecodeEmptyString)
{
    std::string input = "";
    std::string result = Base64Decode(input);
    EXPECT_EQ(result, "");
}

TEST_F(HexTest, Base64DecodeSimpleString)
{
    std::string input = "aGVsbG8=";
    std::string result = Base64Decode(input);
    EXPECT_EQ(result, "hello");
}

TEST_F(HexTest, Base64EncodeDecodeRoundTrip)
{
    std::string original = "The quick brown fox jumps over the lazy dog";
    std::string encoded = Base64Encode(original);
    std::string decoded = Base64Decode(encoded);
    EXPECT_EQ(decoded, original);
}

// ==================== Base64URL Encode Tests ====================

TEST_F(HexTest, Base64UrlEncodeStringEmpty)
{
    std::string input = "";
    std::string result = Base64UrlEncodeString(input);
    EXPECT_EQ(result, "");
}

TEST_F(HexTest, Base64UrlEncodeStringSimple)
{
    std::string input = "hello";
    std::string result = Base64UrlEncodeString(input);
    // Base64 "aGVsbG8=" -> Base64URL "aGVsbG8" (remove =)
    EXPECT_EQ(result, "aGVsbG8");
}

TEST_F(HexTest, Base64UrlEncodeStringWithPlusReplacement)
{
    // This string produces a + in standard Base64
    // Binary: 0xfb, 0xef -> Base64: "++/"
    std::vector<unsigned char> data = {0xfb, 0xef};
    std::string result = Base64UrlEncodeByte(data);
    // + should be replaced with -, / should be replaced with _
    EXPECT_THAT(result, ::testing::Not(::testing::HasSubstr("+")));
    EXPECT_THAT(result, ::testing::Not(::testing::HasSubstr("/")));
}

TEST_F(HexTest, Base64UrlEncodeStringRemovesPadding)
{
    std::string input = "a";  // Base64: "YQ=="
    std::string result = Base64UrlEncodeString(input);
    EXPECT_THAT(result, ::testing::Not(::testing::HasSubstr("=")));
    EXPECT_EQ(result, "YQ");
}

TEST_F(HexTest, Base64UrlEncodeStringJwtHeader)
{
    // Test JWT header encoding
    std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::string result = Base64UrlEncodeString(header);
    EXPECT_FALSE(result.empty());
    EXPECT_THAT(result, ::testing::Not(::testing::HasSubstr("=")));
    EXPECT_THAT(result, ::testing::Not(::testing::HasSubstr("+")));
    EXPECT_THAT(result, ::testing::Not(::testing::HasSubstr("/")));
}

// ==================== Base64URL Decode Tests ====================

TEST_F(HexTest, Base64UrlDecodeEmpty)
{
    std::string input = "";
    std::string result = Base64UrlDecode(input);
    EXPECT_EQ(result, "");
}

TEST_F(HexTest, Base64UrlDecodeSimple)
{
    std::string input = "aGVsbG8";  // "hello" without padding
    std::string result = Base64UrlDecode(input);
    EXPECT_EQ(result, "hello");
}

TEST_F(HexTest, Base64UrlDecodeWithOnePadding)
{
    std::string input = "YWI";  // "ab" needs one padding
    std::string result = Base64UrlDecode(input);
    EXPECT_EQ(result, "ab");
}

TEST_F(HexTest, Base64UrlDecodeWithTwoPadding)
{
    std::string input = "YQ";  // "a" needs two padding
    std::string result = Base64UrlDecode(input);
    EXPECT_EQ(result, "a");
}

TEST_F(HexTest, Base64UrlDecodeWithDashAndUnderscore)
{
    // Encode something that has + and / in standard Base64
    std::vector<unsigned char> original = {0xfb, 0xef, 0xbe};
    std::string encoded = Base64UrlEncodeByte(original);
    std::string decoded = Base64UrlDecode(encoded);

    std::string expected(original.begin(), original.end());
    EXPECT_EQ(decoded, expected);
}

TEST_F(HexTest, Base64UrlEncodeDecodeRoundTrip)
{
    std::string original = "Test string with various characters: 你好世界!@#$%";
    std::string encoded = Base64UrlEncodeString(original);
    std::string decoded = Base64UrlDecode(encoded);
    EXPECT_EQ(decoded, original);
}

TEST_F(HexTest, Base64UrlEncodeDecodeJwtPayload)
{
    std::string payload = R"({"sub":"tenant123","exp":1737849600})";
    std::string encoded = Base64UrlEncodeString(payload);
    std::string decoded = Base64UrlDecode(encoded);
    EXPECT_EQ(decoded, payload);
}

// ==================== HexToBytes Tests ====================

TEST_F(HexTest, HexToBytesEmpty)
{
    std::string hex = "";
    auto result = HexToBytes(hex);
    EXPECT_TRUE(result.empty());
}

TEST_F(HexTest, HexToBytesSimple)
{
    std::string hex = "48656c6c6f";  // "Hello"
    auto result = HexToBytes(hex);
    std::string str(result.begin(), result.end());
    EXPECT_EQ(str, "Hello");
}

TEST_F(HexTest, HexToBytesUpperCase)
{
    std::string hex = "48454C4C4F";  // "HELLO" in uppercase hex
    auto result = HexToBytes(hex);
    std::string str(result.begin(), result.end());
    EXPECT_EQ(str, "HELLO");
}

// ==================== BytesToBase64 Tests ====================

TEST_F(HexTest, BytesToBase64Empty)
{
    std::vector<unsigned char> data = {};
    std::string result = BytesToBase64(data);
    EXPECT_EQ(result, "");
}

TEST_F(HexTest, BytesToBase64Simple)
{
    std::vector<unsigned char> data = {'h', 'e', 'l', 'l', 'o'};
    std::string result = BytesToBase64(data);
    EXPECT_EQ(result, "aGVsbG8=");
}

// ==================== CharStringToHexString Tests ====================

TEST_F(HexTest, CharStringToHexStringEmpty)
{
    std::string input = "";
    std::string result = CharStringToHexString(input);
    EXPECT_EQ(result, "");
}

TEST_F(HexTest, CharStringToHexStringSimple)
{
    std::string input = "A";
    std::string result = CharStringToHexString(input);
    EXPECT_EQ(result, "41");  // 'A' = 0x41
}

TEST_F(HexTest, CharStringToHexStringWithSeparator)
{
    std::string input = "AB";
    std::string result = CharStringToHexString(input, ":");
    EXPECT_EQ(result, "41:42:");  // 'A' = 0x41, 'B' = 0x42
}

}  // namespace functionsystem::test
