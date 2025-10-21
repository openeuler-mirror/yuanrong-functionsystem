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

#ifndef OBSERVABILITY_SDK_LOGS_LOG_HANDLER_H
#define OBSERVABILITY_SDK_LOGS_LOG_HANDLER_H

#include "logs/api/log_param.h"

namespace observability::sdk::logs {

void LogRollingCompress(const observability::api::logs::LogParam &logParam);
void DoLogFileRolling(const observability::api::logs::LogParam &logParam);
void DoLogFileCompress(const observability::api::logs::LogParam &logParam);

}  // namespace observability::sdk::logs

#endif  // LOGS_LOGGER_HANDLER_H