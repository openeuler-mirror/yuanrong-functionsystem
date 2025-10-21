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

#ifndef __SYSMGR_ACTOR_HPP__
#define __SYSMGR_ACTOR_HPP__

#include <queue>

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "actor/actorapp.hpp"

namespace litebus {

const std::string SYSMGR_ACTOR_NAME = "SysMgrActor";
const std::string METRICS_SEND_MSGNAME = "SendMetrics";
const int LINK_RECYCLE_PERIOD_MIN = 20;
const int LINK_RECYCLE_PERIOD_MAX = 360;

using IntTypeMetrics = std::queue<int>;
using StringTypeMetrics = std::queue<std::string>;

class MetricsMessage : public MessageBase {
public:
    explicit MetricsMessage(const std::string &tfrom, const std::string &tTo, const std::string &tName,
                            const IntTypeMetrics &tInts = IntTypeMetrics(),
                            const StringTypeMetrics &tStrings = StringTypeMetrics())
        : MessageBase(tfrom, tTo, tName), intTypeMetrics(tInts), stringTypeMetrics(tStrings)
    {
    }

    ~MetricsMessage() override
    {
    }

    void PrintMetrics();

private:
    IntTypeMetrics intTypeMetrics;
    StringTypeMetrics stringTypeMetrics;
};

class SysMgrActor : public litebus::AppActor {
public:
    explicit SysMgrActor(const std::string &name, const Duration &duration)
        : litebus::AppActor(name), printSendMetricsDuration(duration)
    {
    }

    ~SysMgrActor() override
    {
    }

protected:
    void Init() override
    {
        BUSLOG_INFO("Initiaize SysMgrActor");

        // register receive handle
        Receive("SendMetrics", &SysMgrActor::HandleSendMetricsCallback);

        // start sys manager timers
        (void)AsyncAfter(printSendMetricsDuration, GetAID(), &SysMgrActor::SendMetricsDurationCallback);

        char *linkRecycleEnv = getenv("LITEBUS_LINK_RECYCLE_PERIOD");
        if (linkRecycleEnv != nullptr) {
            int period = 0;
            try {
                period = std::stoi(linkRecycleEnv);
            } catch (...) {
                BUSLOG_ERROR("stoi fail:{}", linkRecycleEnv);
                period = 0;
            }
            if (period >= LINK_RECYCLE_PERIOD_MIN && period <= LINK_RECYCLE_PERIOD_MAX) {
                BUSLOG_INFO("link recycle set:{}", period);
                linkRecyclePeriod = period;
                (void)AsyncAfter(linkRecycleDuration, GetAID(), &SysMgrActor::LinkRecycleDurationCallback);
            }
        }
    }

private:
    void SendMetricsDurationCallback();
    void HandleSendMetricsCallback(const AID &from, std::unique_ptr<MetricsMessage> message);
    void LinkRecycleDurationCallback();
    Duration printSendMetricsDuration;
    static Duration linkRecycleDuration;
    int linkRecyclePeriod = 0;
};

}    // namespace litebus
#endif
