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

#ifndef COMMON_UTILS_EXCEPTION_H
#define COMMON_UTILS_EXCEPTION_H

#include <unistd.h>
#include <csignal>

namespace functionsystem {
void RegisterGracefulExit(sighandler_t handler);
void RegisterSigHandler();
pid_t GetTid();
void RecordBackTrace(int sig);
}  // namespace functionsystem

#endif  // COMMON_UTILS_EXCEPTION_H
