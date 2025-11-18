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
#include "watcher.h"

namespace functionsystem {
Watcher::Watcher(const std::function<void(int64_t)> &method)
{
    closeMethod_ = method;
}

int64_t Watcher::GetWatchId() const
{
    return watchId_;
}

void Watcher::SetWatchId(int64_t watchId)
{
    if (watchId_ == -1 && !canceled_) {
        watchId_ = watchId;
    }
}

bool Watcher::IsCanceled() const
{
    return canceled_;
}

// cancel observer
void Watcher::Close() noexcept
{
    if (canceled_) {
        return;
    }

    canceled_ = true;
    closeMethod_(watchId_);
}

void Watcher::Reset() noexcept
{
    watchId_ = -1;
}
}  // namespace functionsystem
