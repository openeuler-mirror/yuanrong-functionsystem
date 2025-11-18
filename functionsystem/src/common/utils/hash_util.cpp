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

#include "hash_util.h"

#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <openssl/md5.h>

#include "common/logs/logging.h"
#include "common/utils/files.h"

namespace functionsystem {
constexpr size_t HEX_WIDTH = 2;

// Function to convert a hash value to a fixed-length hexadecimal string
inline std::string HashToFixedHex(const std::size_t& hashVal, size_t width = sizeof(std::size_t) * 2)
{
    std::stringstream ss;
    ss << std::hex << std::setw(width) << std::setfill('0')
       << hashVal;  // Convert the hash value to hex with leading zeros
    return ss.str();
}

// Function to hash a string and return the result as a fixed-length hex string
std::string GetHashString(const std::string& input)
{
    std::hash<std::string> hasher;
    std::size_t hashVal = hasher(input);  // Hash the input string
    return HashToFixedHex(hashVal);
}

// Calculate the MD5 checksum of a file
std::string CalculateFileMD5(const std::string& filePath)
{
    if (!IsFile(filePath)) {
        YRLOG_ERROR("Failed to open file: {}", filePath);
        return "";
    }
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        YRLOG_ERROR("Failed to open file: {}", filePath);
        return "";
    }

    // Read file content into a buffer
    std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
    file.close();

    // Calculate MD5
    unsigned char result[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char *>(buffer.data()), buffer.size(), result);

    // Convert the result to a hexadecimal string
    char md5String[MD5_DIGEST_LENGTH * HEX_WIDTH + 1] = {0}; // MD5_DIGEST_LENGTH is 16， 16 * 2 + 1 = 33
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf_s(&md5String[i * HEX_WIDTH], MD5_DIGEST_LENGTH, "%02x", static_cast<unsigned int>(result[i]));
    }
    return std::string(md5String);
}

}  // namespace functionsystem