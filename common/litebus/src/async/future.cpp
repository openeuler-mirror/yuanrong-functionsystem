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

#include "async/future.hpp"
#include "actor/buslog.hpp"

namespace litebus {
namespace internal {

void Waitf(const AID &aid)
{
    litebus::Terminate(aid);
    BUSLOG_WARN("WaitFor is timeout.");
}

void Wait(const AID &aid, const litebus::Timer &timer)
{
    (void)litebus::TimerTools::Cancel(timer);
    litebus::Terminate(aid);
}

}    // namespace internal
}    // namespace litebus
