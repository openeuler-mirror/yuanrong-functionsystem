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

#ifndef COMMON_UTILS_RANDOM_NUMBER_H
#define COMMON_UTILS_RANDOM_NUMBER_H

#include <random>

namespace functionsystem {

template <class T>
T GenerateRandomNumber(T lower, T upper)
{
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<T> dis(lower, upper);
    return static_cast<T>(dis(mt));
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_RANDOM_NUMBER_H
