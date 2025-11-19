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

#ifndef FUNCTION_MASTER_SCALER_CONSTANTS_H
#define FUNCTION_MASTER_SCALER_CONSTANTS_H

namespace functionsystem::scaler {
const uint32_t POD_NAME_MAX_LENGTH = 63;
const std::string POOL_NAME_PREFIX = "function-agent-";
const std::string CPU_RESOURCE = "cpu";
const std::string MEMORY_RESOURCE = "memory";
const std::string GPU_RESOURCE = "gpu";
const std::string REUSE_LABEL_KEY = "yr-reuse";
const std::string PENDING_THRESHOLD = "yr-pod-pending-duration-threshold";
const std::string POD_POOL_ID = "yr-pod-pool";
const std::string APP_LABEL_KEY = "app";
}

#endif  // FUNCTION_MASTER_SCALER_CONSTANTS_H
