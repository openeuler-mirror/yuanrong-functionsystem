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
}

#endif  // SRC_COMMON_CONSTANTS_SIGNAL_H
