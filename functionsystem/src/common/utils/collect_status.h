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
#ifndef COMMON_UTILS_COLLECT_STATUS_H
#define COMMON_UTILS_COLLECT_STATUS_H

#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/status/status.h"

namespace functionsystem {
[[maybe_unused]] static litebus::Future<Status> CollectStatus(
    const std::list<litebus::Future<Status>> &futures, const std::string &description,
    const StatusCode &concernedCode = StatusCode::FAILED,
    const StatusCode &defaultCode = StatusCode::ERR_INNER_SYSTEM_ERROR)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)litebus::Collect<Status>(futures).OnComplete(
        [promise, description, concernedCode, defaultCode](const litebus::Future<std::list<Status>> &future) {
            if (future.IsError()) {
                promise->SetValue(Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to " + description));
                return;
            }
            bool isError = false;
            bool isConcerned = false;
            auto result = Status::OK();
            StatusCode errCode = defaultCode;
            std::set<std::string> errs;
            for (auto status : future.Get()) {
                if (status.IsOk()) {
                    continue;
                }
                if (status.StatusCode() == concernedCode) {
                    isConcerned = true;
                }
                isError = true;
                if (errCode == StatusCode::ERR_INNER_SYSTEM_ERROR) {
                    errCode = status.StatusCode();
                }
                // deduplication
                (void)errs.emplace(status.GetMessage());
            }
            if (isError) {
                for (const auto &err : errs) {
                    result.AppendMessage(err);
                }
                promise->SetValue(Status(isConcerned ? concernedCode : errCode, result.GetMessage()));
                return;
            }
            promise->SetValue(result);
        });
    return promise->GetFuture();
}
}  // namespace functionsystem
#endif  // COMMON_UTILS_COLLECT_STATUS_H