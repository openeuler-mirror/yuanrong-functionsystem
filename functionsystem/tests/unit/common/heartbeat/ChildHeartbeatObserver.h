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

#ifndef YUANRONG_KERNEL_CHILDHEARTBEATOBSERVER_H
#define YUANRONG_KERNEL_CHILDHEARTBEATOBSERVER_H

#endif // YUANRONG_KERNEL_CHILDHEARTBEATOBSERVER_H

#include "heartbeat/heartbeat_observer.h"
namespace functionsystem {
class ChildHeartbeatObserver : public HeartbeatObserver {
public:
    [[maybe_unused]] explicit ChildHeartbeatObserver(const std::string &name, const litebus::AID &dst,
        const HeartbeatObserver::TimeOutHandler &handler);

    void Exited(const litebus::AID &actor) override
    {
        HeartbeatObserver::Exited(actor);
    }

    ~ChildHeartbeatObserver() override = default;
};
}