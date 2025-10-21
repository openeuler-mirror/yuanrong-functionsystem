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

#include "view_utils.h"

namespace functionsystem::test::view_utils {

const std::string RESOURCE_CPU_NAME = "CPU";
const std::string RESOURCE_MEM_NAME = "Memory";
const double SCALA_VALUE0 = 0.0;
const double SCALA_VALUE1 = 1000.1;
const double SCALA_VALUE2 = 1000.1;

const double INST_SCALA_VALUE = 10.1;

const std::string CPU_SCALA_RESOURCE_STRING = "{" + RESOURCE_CPU_NAME + ":" +
                                              std::to_string(static_cast<int64_t>(SCALA_VALUE1)) + ":" +
                                              std::to_string(static_cast<int64_t>(SCALA_VALUE2)) + "}";
const std::string CPU_SCALA_RESOURCES_STRING = "{ {" + RESOURCE_CPU_NAME + ":" +
                                               std::to_string(static_cast<int64_t>(SCALA_VALUE1)) + ":" +
                                               std::to_string(static_cast<int64_t>(SCALA_VALUE2)) + "} }";

}  // namespace functionsystem::test::view_utils