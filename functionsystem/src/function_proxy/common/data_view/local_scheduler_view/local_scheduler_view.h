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
#ifndef FUNCTION_PROXY_COMMON_LOCAL_SCHEDULER_VIEW_H
#define FUNCTION_PROXY_COMMON_LOCAL_SCHEDULER_VIEW_H

#include <actor/aid.hpp>
#include <optional>
#include <unordered_map>

#include "common/status/status.h"

namespace functionsystem::function_proxy {
class LocalSchedulerView {
public:
    LocalSchedulerView() = default;

    ~LocalSchedulerView() = default;
    std::shared_ptr<litebus::AID> Get(const std::string &proxyID);
    void Update(const std::string &proxyID, const std::shared_ptr<litebus::AID> &aid);
    void Delete(const std::string &proxyID);
    void Clear();

private:
    std::unordered_map<std::string, std::shared_ptr<litebus::AID>> localSchedulers_;
};
}  // namespace functionsystem::function_proxy
#endif
