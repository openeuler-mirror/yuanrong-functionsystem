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

#ifndef COMMON_UTILS_CAPABILITY_H
#define COMMON_UTILS_CAPABILITY_H

#include <sys/capability.h>

#include "common/logs/logging.h"

namespace functionsystem {
static void ClearCapabilities(const cap_value_t *capList, int ncap)
{
    cap_t caps = cap_get_proc();
    if (cap_set_flag(caps, CAP_INHERITABLE, ncap, capList, CAP_CLEAR) == -1) {
        YRLOG_WARN("Failed to remove capability from inheritable set.");
    }

    if (cap_set_flag(caps, CAP_EFFECTIVE, ncap, capList, CAP_CLEAR) == -1) {
        YRLOG_WARN("Failed to remove capability from effective set.");
    }

    if (cap_set_flag(caps, CAP_PERMITTED, ncap, capList, CAP_CLEAR) == -1) {
        YRLOG_WARN("Failed to remove capability from permitted set.");
    }

    if (cap_set_proc(caps) == -1) {
        YRLOG_WARN("Failed to apply new capabilities");
    }
    (void)cap_free(caps);
}
}  // namespace functionsystem

#endif  // COMMON_UTILS_CAPABILITY_H