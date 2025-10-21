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

#ifndef OBSERVABILITY_SDK_LOGS_LOG_PARAM_PARSER_H
#define OBSERVABILITY_SDK_LOGS_LOG_PARAM_PARSER_H

#include <memory>
#include <string>

#include "logs/api/log_param.h"

namespace observability::sdk::logs {

observability::api::logs::LogParam GetLogParam(const std::string &configJsonString, const std::string &nodeName,
    const std::string &modelName, const bool logFileWithTime = false, const std::string &fileNamePattern = "");

std::string GetLogFile(const observability::api::logs::LogParam &param);

observability::api::logs::GlobalLogParam GetGlobalLogParam(const std::string &configJsonString);
}  // namespace observability::sdk::logs

#endif