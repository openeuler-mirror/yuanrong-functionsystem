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

#include "hex.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "utils/os_utils.hpp"

namespace functionsystem {
const static int32_t BIT_SIZE_OF_TWO_BYTE = 16;
const uint8_t HEX_TO_BYTE_MIN_LEN = 2;
std::vector<unsigned char> HexToBytes(const std::string &hex)
{
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += HEX_TO_BYTE_MIN_LEN) {
        std::string byte_str = hex.substr(i, HEX_TO_BYTE_MIN_LEN);
        auto byte = static_cast<unsigned char>(std::stoul(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string BytesToBase64(const std::vector<unsigned char> &data)
{
    BIO *bio;
    BIO *b64;
    BUF_MEM *buffer_ptr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    (void)BIO_write(bio, data.data(), data.size());
    (void)BIO_flush(bio);
    (void)BIO_get_mem_ptr(bio, &buffer_ptr);

    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);

    return result;
}

std::string Base64UrlEncode(const std::string &data)
{
    // Base64Url: replace + with -, / with _, and remove the trailing =
    std::string result = data;
    // replace + with -
    std::replace(result.begin(), result.end(), '+', '-');
    // replace / with _
    std::replace(result.begin(), result.end(), '/', '_');
    // remove the trailing =
    result.erase(std::remove(result.begin(), result.end(), '='), result.end());
    return result;
}

std::string Base64UrlEncodeByte(const std::vector<unsigned char> &data)
{
    return Base64UrlEncode(BytesToBase64(data));
}

std::string Base64UrlEncodeString(const std::string &data)
{
    std::vector<unsigned char> vec(data.begin(), data.end());
    return Base64UrlEncodeByte(vec);
}

std::string Base64Decode(const std::string &input)
{
    if (input.empty()) {
        return {};
    }
    BIO *bio;
    BIO *b64;
    char buffer[input.length()] = { 0 };
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(input.c_str(), -1);
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    int decodedLength = BIO_read(bio, buffer, input.length());
    BIO_free_all(bio);
    std::string result(buffer, decodedLength);
    return result;
}

std::string Base64Encode(const std::string &input)
{
    if (input.empty()) {
        return {};
    }
    BUF_MEM *bptr = nullptr;
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    (void)BIO_write(b64, input.c_str(), static_cast<int>(input.length()));
    (void)BIO_flush(b64);
    (void)BIO_get_mem_ptr(b64, &bptr);

    char buff[bptr->length + 1] = { 0 };
    auto ret = memcpy_s(buff, bptr->length + 1, bptr->data, bptr->length);
    if (ret != 0) {
        BIO_free_all(b64);
        YRLOG_ERROR("copy base64 failed. {}", litebus::os::Strerror(errno));
        return {};
    }
    buff[bptr->length] = 0;
    std::string encode(buff, bptr->length);
    BIO_free_all(b64);
    return encode;
}

std::string FromHexString(const std::string &hex)
{
    std::string result;
    for (size_t i = 0; i < hex.length(); i += CHAR_TO_HEX) {
        std::string byteString = hex.substr(i, CHAR_TO_HEX);  // 2 hex
        char byte = static_cast<char>(strtol(byteString.c_str(), nullptr, BIT_SIZE_OF_TWO_BYTE));
        result.push_back(byte);
    }
    return result;
}

std::string HexStringToCharString(const std::string &hexString)
{
    // hex string doest have '\n'
    std::string hexStringRef = hexString;
    if (!hexStringRef.empty() && hexStringRef.at(hexStringRef.length() - 1) == '\n') {
        hexStringRef.pop_back();
    }

    return FromHexString(hexStringRef);
}

Status HexStringToCharString(const char *hexString, char *charString, unsigned int charStringLen)
{
    if (hexString == nullptr || charString == nullptr) {
        return Status(StatusCode::FAILED, "pointer is nullptr");
    }

    auto strHexLen = strlen(hexString);
    if ((strHexLen == 0) || (strHexLen % CHAR_TO_HEX != 0)) {
        return Status(StatusCode::FAILED, "The number of text for decode must be an even number and greater than zero");
    }

    std::string temp = FromHexString(hexString);

    int ret = memcpy_s(charString, charStringLen, temp.c_str(), temp.length());
    if (ret != 0) {
        return Status(StatusCode::FAILED, "failed to copy char#" + std::to_string(ret));
    }

    return Status::OK();
}

Status HexStringToCharStringCap(const unsigned char *hexString, uint32_t hexStringLen, char *charString,
                                uint32_t charStringLen)
{
    if (hexString == nullptr || charString == nullptr) {
        return Status(StatusCode::FAILED, "pointer is nullptr");
    }

    if ((hexStringLen == 0) || (hexStringLen % CHAR_TO_HEX != 0)) {
        return Status(StatusCode::FAILED, "The number of text for decode must be an even number and greater than zero");
    }

    std::string temp;
    for (unsigned int i = 0; i < hexStringLen; i += CHAR_TO_HEX) {
        temp += static_cast<char>((HEX_STRING_SET_CAP.find(static_cast<char>(hexString[i])) << FIRST_FOUR_BIT_MOVE)
                                  | (HEX_STRING_SET_CAP.find(static_cast<char>(hexString[i + 1]))));
    }

    int ret = memcpy_s(charString, charStringLen, temp.c_str(), temp.length());
    if (ret != 0) {
        return Status(StatusCode::FAILED, "failed to copy char#" + std::to_string(ret));
    }

    return Status::OK();
}

Status HexStringToCharStringCap(const char *hexStr, uint32_t hexStrLen, char *charStr, uint32_t charStrLen)
{
    if (hexStr == nullptr || charStr == nullptr) {
        return Status(StatusCode::FAILED, "pointer is nullptr");
    }

    if ((hexStrLen == 0) || (hexStrLen % CHAR_TO_HEX != 0)) {
        return Status(StatusCode::FAILED, "The number of text for decode must be an even number and greater than zero");
    }

    std::string temp;
    for (unsigned int i = 0; i < hexStrLen; i += CHAR_TO_HEX) {
        temp += static_cast<char>((HEX_STRING_SET_CAP.find(hexStr[i]) << FIRST_FOUR_BIT_MOVE)
                                  | (HEX_STRING_SET_CAP.find(hexStr[i + 1])));
    }

    int ret = memcpy_s(charStr, charStrLen, temp.c_str(), temp.length());
    if (ret != 0) {
        return Status(StatusCode::FAILED, "failed to copy char#" + std::to_string(ret));
    }

    return Status::OK();
}
}  // namespace functionsystem