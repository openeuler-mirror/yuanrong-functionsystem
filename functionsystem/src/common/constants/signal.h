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

#ifndef SRC_COMMON_CONSTANTS_SIGNAL_H
#define SRC_COMMON_CONSTANTS_SIGNAL_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace functionsystem {
// Minimum signal range
const int32_t MIN_SIGNAL_NUM = 1;
// Maximum signal range
const int32_t MAX_SIGNAL_NUM = 1024;
// Minimum value of user-defined signal
const int32_t MIN_USER_SIGNAL_NUM = 64;

// kill an instance
const int32_t SHUT_DOWN_SIGNAL = 1;
// kill all instances of a job
const int32_t SHUT_DOWN_SIGNAL_ALL = 2;
// kill an instance synchronously
const int32_t SHUT_DOWN_SIGNAL_SYNC = 3;
// kill group
const int32_t SHUT_DOWN_SIGNAL_GROUP = 4;
// set instance to FATAL, when use set fatal signal, the payload will be writen into the instance exit message
const int32_t GROUP_EXIT_SIGNAL = 5;
const int32_t FAMILY_EXIT_SIGNAL = 6;
const int32_t APP_STOP_SIGNAL = 7;
const int32_t REMOVE_RESOURCE_GROUP = 8;
// Subscription-related signals
const int32_t SUBSCRIBE_SIGNAL = 9;
const int32_t NOTIFY_SIGNAL = 10;
const int32_t UNSUBSCRIBE_SIGNAL = 11;
// signal for instance to making checkpint
const int32_t INSTANCE_CHECKPOINT_SIGNAL = 12;
// signal for instance to be suspend state after making checkpoint
const int32_t INSTANCE_TRANS_SUSPEND_SIGNAL = 13;
// signal for instance to be suspend (checkpoint & state change & resource release)
const int32_t INSTANCE_SUSPEND_SIGNAL = 14;
// signal for instance resume (todo)
const int32_t INSTANCE_RESUME_SIGNAL = 15;
// signal for group suspend
const int32_t GROUP_SUSPEND_SIGNAL = 16;
// signal for group resume
const int32_t GROUP_RESUME_SIGNAL = 17;

inline std::string SignalToString(int32_t signal)
{
    static std::unordered_map<int32_t, std::string> signalMap = {
        { SHUT_DOWN_SIGNAL, "SHUT_DOWN_SIGNAL" },
        { SHUT_DOWN_SIGNAL_ALL, "SHUT_DOWN_SIGNAL_ALL" },
        { SHUT_DOWN_SIGNAL_SYNC, "SHUT_DOWN_SIGNAL_SYNC" },
        { SHUT_DOWN_SIGNAL_GROUP, "SHUT_DOWN_SIGNAL_GROUP" },
        { GROUP_EXIT_SIGNAL, "GROUP_EXIT_SIGNAL" },
        { FAMILY_EXIT_SIGNAL, "FAMILY_EXIT_SIGNAL" },
        { APP_STOP_SIGNAL, "APP_STOP_SIGNAL" },
        { REMOVE_RESOURCE_GROUP, "REMOVE_RESOURCE_GROUP" },
        { SUBSCRIBE_SIGNAL, "SUBSCRIBE_SIGNAL" },
        { NOTIFY_SIGNAL, "NOTIFY_SIGNAL" },
        { UNSUBSCRIBE_SIGNAL, "UNSUBSCRIBE_SIGNAL" },
        { INSTANCE_CHECKPOINT_SIGNAL, "INSTANCE_CHECKPOINT_SIGNAL" },
        { INSTANCE_TRANS_SUSPEND_SIGNAL, "INSTANCE_TRANS_SUSPEND_SIGNAL" },
        { INSTANCE_SUSPEND_SIGNAL, "INSTANCE_SUSPEND_SIGNAL" },
        { INSTANCE_RESUME_SIGNAL, "INSTANCE_RESUME_SIGNAL" },
        { GROUP_SUSPEND_SIGNAL, "GROUP_SUSPEND_SIGNAL" },
        { GROUP_RESUME_SIGNAL, "GROUP_RESUME_SIGNAL" },
    };
    return signalMap.find(signal) != signalMap.end() ? signalMap.at(signal) : "UnknownSignal";
}
}

#endif  // SRC_COMMON_CONSTANTS_SIGNAL_H
