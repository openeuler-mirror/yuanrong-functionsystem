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

#ifndef UUID_BASE_HPP
#define UUID_BASE_HPP

#include <securec.h>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <iomanip>
#include "async/option.hpp"

namespace litebus {
namespace uuids {

const std::size_t UUID_SIZE = 16;

struct uuid {
public:
    static std::size_t Size();

    static std::string ToBytes(const uuid &u);

    static Option<uuid> FromBytes(const std::string &s);

    static Option<unsigned char> GetValue(char c);

    static Option<uuid> FromString(const std::string &s);

    // To check whether uuid looks like 0000000-000-000-000-000000000000000
    bool IsNilUUID() const;

    const uint8_t *Get() const;

private:
    const uint8_t *BeginAddress() const;

    const uint8_t *EndAddress() const;

    uint8_t *BeginAddress();

    uint8_t *EndAddress();

    friend class RandomBasedGenerator;
    friend inline bool operator==(uuid const &left, uuid const &right);
    friend inline bool operator!=(uuid const &left, uuid const &right);
    template <typename T, typename F>
    friend std::basic_ostream<T, F> &operator<<(std::basic_ostream<T, F> &s, const struct uuid &outputUuid);
    uint8_t uuidData[UUID_SIZE];
};

class RandomBasedGenerator {
public:
    static uuid GenerateRandomUuid();
};

// operator override
inline bool operator==(uuid const &left, uuid const &right)
{
    return std::equal(left.BeginAddress(), left.EndAddress(), right.BeginAddress());
}

// operator override
inline bool operator!=(uuid const &left, uuid const &right)
{
    return !(left == right);
}

// operator override
template <typename T, typename F>
std::basic_ostream<T, F> &operator<<(std::basic_ostream<T, F> &s, const struct uuid &outputUuid)
{
    const int firstDelimOffset = 3;
    const int secondDelimOffset = 5;
    const int thirdDelimOffset = 7;
    const int fourthDelimOffset = 9;
    const int uuidWidth = 2;
    s << std::hex << std::setfill(static_cast<T>('0'));

    int i = 0;
    for (const uint8_t *ptr = outputUuid.BeginAddress(); ptr < outputUuid.EndAddress(); ++ptr, ++i) {
        s << std::setw(uuidWidth) << static_cast<int>(*ptr);
        if (i == firstDelimOffset || i == secondDelimOffset ||
            i == thirdDelimOffset || i == fourthDelimOffset) {
            s << '-';
        }
    }

    s << std::setfill(static_cast<T>(' ')) << std::dec;
    return s;
}

}    // namespace uuids
}    // namespace litebus

#endif /* UUID_BASE_HPP */
