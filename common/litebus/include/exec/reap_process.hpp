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

#ifndef LITEBUS_EXEC_REAP_HPP
#define LITEBUS_EXEC_REAP_HPP

#include <async/future.hpp>
#include <async/try.hpp>
#include <map>
#include <unordered_map>
#include "actor/buslog.hpp"

namespace litebus {

class ReaperActor : public ActorBase {
public:
    ReaperActor(const std::string &name) : ActorBase(name)
    {
        BUSLOG_INFO("ReaperActor Created.");
    };
    void ReapStatus(bool withTimer = true);
    ~ReaperActor() override;

protected:
    void Finalize() override;
};

Future<Option<int>> ReapInActor(pid_t pid);

// Notify litebus ReaperActor that an external reaper (e.g. HealthCheckActor in
// merge_process mode) has already waited on `pid` and obtained `status`. If the
// pid has a pending promise, fulfill it with the real status and erase it from
// the internal map. Returns true when a promise was found and notified.
// Safe to call from any thread; takes the internal promise lock.
bool TryNotifyExternalReap(pid_t pid, int status);

}    // namespace litebus

#endif
