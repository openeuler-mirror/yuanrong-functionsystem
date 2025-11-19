
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

#ifndef FUNCTIONSYSTEM_COMMON_UTILS_RAII_H
#define FUNCTIONSYSTEM_COMMON_UTILS_RAII_H

#include <functional>

#include "common/logs/logging.h"

namespace functionsystem {
class Raii {
public:
    explicit Raii(std::function<void(void)> function) : function_(std::move(function))
    {
    }

    ~Raii() noexcept
    {
        try {
            function_();
        } catch (std::exception &e) {
            YRLOG_ERROR("failed to call function in Raii, error: {}", e.what());
        } catch (...) {
            YRLOG_ERROR("failed to call function in Raii, unknown error");
        }
    }

private:
    std::function<void(void)> function_;
};
}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_COMMON_UTILS_RAII_H
