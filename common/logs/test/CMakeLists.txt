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

enable_testing()
include(GoogleTest)

set(TEST_MODULE logs_test)

add_library(api_test SHARED api/logger_provider_test.cpp)
add_dependencies(api_test gtest_1_12_1 spdlog)

target_link_libraries(api_test PUBLIC ${gtest_LIB_A} ${gmock_LIB_A}
                                      observability-logs-headers pthread)

add_executable(${TEST_MODULE} main.cpp sdk/logger_provider_sdk_test.cpp
                              sdk/log_param_test.cpp sdk/log_manager_test.cpp)

target_compile_options(${TEST_MODULE} PRIVATE -Wno-error)

target_link_libraries(${TEST_MODULE} PRIVATE api_test yrlogs)

target_include_directories(${TEST_MODULE}
                           PRIVATE "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>")

gtest_discover_tests(${TEST_MODULE})
