# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if ("${THIRDPARTY_CONFIG_DIR}" STREQUAL "")
message(STATUS "use default thirdparty config dir")
    set(THIRDPARTY_CONFIG_DIR ${PROJECT_SOURCE_DIR}/../../thirdparty/thirdparty)
endif()

message(STATUS "THIRDPARTY_CONFIG_DIR is ${THIRDPARTY_CONFIG_DIR}")

if (NOT EXISTS ${THIRDPARTY_CONFIG_DIR})
    message(FATAL_ERROR "THIRDPARTY_CONFIG_DIR: ${THIRDPARTY_CONFIG_DIR} does not exist, please download it manually.")
endif()

list(APPEND CMAKE_MODULE_PATH ${THIRDPARTY_CONFIG_DIR}/cmake)

include(third_utils)
include(zlib)
include(spdlog)
include(cjson)

if (LOGS_BUILD_TEST)
    include(gtest_1_12_1)
endif()
