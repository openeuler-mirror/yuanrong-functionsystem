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

#ifndef RUNTIME_MANAGER_UTILS_UTILS_H
#define RUNTIME_MANAGER_UTILS_UTILS_H

#include <vector>
#include <async/uuid_generator.hpp>
#include <functional>

namespace functionsystem::runtime_manager {
class Utils {
public:
    static std::string JoinToString(std::vector<std::string> const &strings, std::string delim);

    static std::string TrimPrefix(const std::string &str, const std::string &prefix);

    static std::string GetJobIDFromTraceID(const std::string &traceID);

    static std::vector<std::string> SplitByFunc(std::string str, const std::function<bool(const char &)> &func);

    static std::string LinkCommandWithLdLibraryPath(const std::string& ldLibraryPath, const std::string& originCmd);
};
}  // namespace functionsystem::runtime_manager

#endif // RUNTIME_MANAGER_UTILS_UTILS_H
