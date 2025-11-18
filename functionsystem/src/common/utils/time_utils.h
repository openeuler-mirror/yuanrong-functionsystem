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


#ifndef COMMON_UTILS_TIME_H
#define COMMON_UTILS_TIME_H

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace functionsystem {
std::tm ParseTimestamp(const std::string &timestamp);
bool IsLaterThan(const std::string &timestamp1, const std::string &timestamp2, double seconds);
}  // namespace functionsystem
#endif  // COMMON_UTILS_TIME_H
