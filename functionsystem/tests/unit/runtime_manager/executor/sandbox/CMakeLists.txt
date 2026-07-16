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
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SANDBOX_TEST_SRCS)
target_sources(${UNIT_TEST_MODULE} PRIVATE ${SANDBOX_TEST_SRCS})

set(SANDBOX_FOCUSED_TEST_COMMON_SRCS
    ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
)

set(SANDBOX_REGRESSION_TEST_SRCS
    sandbox_command_utils_test.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../sandbox_executor_network_test.cpp
    sandbox_executor_retry_test.cpp
    sandbox_executor_test.cpp
    sandbox_executor_trace_test.cpp
    sandbox_request_builder_test.cpp
    runtime_state_manager_test.cpp
)

add_executable(sandbox_executor_metrics_test_bin
    ${SANDBOX_FOCUSED_TEST_COMMON_SRCS}
    sandbox_executor_metrics_test.cpp
)
add_executable(sandbox_executor_regression_test_bin
    ${SANDBOX_FOCUSED_TEST_COMMON_SRCS}
    ${SANDBOX_REGRESSION_TEST_SRCS}
)

target_compile_options(sandbox_executor_metrics_test_bin
    PRIVATE
    -Wno-error
    -fno-access-control
    -Wno-missing-field-initializers
)
target_compile_options(sandbox_executor_regression_test_bin
    PRIVATE
    -Wno-error
    -fno-access-control
    -Wno-missing-field-initializers
)
target_include_directories(sandbox_executor_metrics_test_bin PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${SRC_INCLUDE_DIR}
    ${SRC_RUNTIME_MANAGER_INCLUDE_DIR}
    ${gtest_INCLUDE_DIR}
)
target_include_directories(sandbox_executor_regression_test_bin PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${SRC_INCLUDE_DIR}
    ${SRC_RUNTIME_MANAGER_INCLUDE_DIR}
    ${gtest_INCLUDE_DIR}
)
target_link_libraries(sandbox_executor_metrics_test_bin PRIVATE
    runtime_manager_lib
    metrics_adapter
    ${gtest_LIB_A}
    ${gmock_LIB_A}
    pthread
)
target_link_libraries(sandbox_executor_regression_test_bin PRIVATE
    runtime_manager_lib
    metrics_adapter
    ${gtest_LIB_A}
    ${gmock_LIB_A}
    pthread
)
add_test(NAME sandbox_executor_metrics_test_bin COMMAND sandbox_executor_metrics_test_bin)
add_test(NAME sandbox_executor_regression_test_bin COMMAND sandbox_executor_regression_test_bin)
