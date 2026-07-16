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

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} GLOBAL_SCHEDULER_TEST_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler_manager GLOBAL_SCHEDULER_TEST_SRCS)

target_sources(${UNIT_TEST_MODULE} PRIVATE
        ${GLOBAL_SCHEDULER_TEST_SRCS})

# Route-cache policy is a narrow boundary and should remain verifiable without
# building the repository-wide unit-test aggregate.
add_executable(traefik_route_cache_test_bin
        ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/traefik_route_cache_test.cpp
        ${PROJECT_SOURCE_DIR}/src/function_master/global_scheduler/traefik_route_cache.cpp)
target_compile_options(traefik_route_cache_test_bin PRIVATE
        -Wno-error
        -fno-access-control
        -Wno-missing-field-initializers)
target_include_directories(traefik_route_cache_test_bin PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${gtest_INCLUDE_DIR})
target_link_libraries(traefik_route_cache_test_bin PRIVATE
        utils
        resource_view
        ${litebus_ALL_LIB}
        ${yrlogs_LIB}
        ${gtest_LIB_A}
        ${gmock_LIB_A}
        pthread)
add_test(NAME traefik_route_cache_test_bin COMMAND traefik_route_cache_test_bin)
