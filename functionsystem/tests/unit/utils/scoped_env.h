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

#ifndef FUNCTIONSYSTEM_TESTS_UNIT_UTILS_SCOPED_ENV_H
#define FUNCTIONSYSTEM_TESTS_UNIT_UTILS_SCOPED_ENV_H

#include <optional>
#include <string>
#include <utility>

#include "utils/os_utils.hpp"

namespace functionsystem::test {

class ScopedEnv {
public:
    explicit ScopedEnv(std::string name) : name_(std::move(name))
    {
        auto value = litebus::os::GetEnv(name_);
        if (value.IsSome()) {
            originalValue_ = value.Get();
        }
    }

    ~ScopedEnv()
    {
        if (originalValue_.has_value()) {
            litebus::os::SetEnv(name_, originalValue_.value());
        } else {
            litebus::os::UnSetEnv(name_);
        }
    }

    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

    void Set(const std::string &value) const
    {
        litebus::os::SetEnv(name_, value);
    }

    void Unset() const
    {
        litebus::os::UnSetEnv(name_);
    }

private:
    std::string name_;
    std::optional<std::string> originalValue_;
};

}  // namespace functionsystem::test

#endif  // FUNCTIONSYSTEM_TESTS_UNIT_UTILS_SCOPED_ENV_H
