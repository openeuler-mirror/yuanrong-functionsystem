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
include_directories(${CMAKE_CURRENT_LIST_DIR})

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} INTEGRATION_TEST_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/utils INTEGRATION_TEST_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/common/resource_view INTEGRATION_TEST_SRCS)

set(INTEGRATION_TEST_MODULE functionsystem_integration_test)

add_executable(${INTEGRATION_TEST_MODULE} ${INTEGRATION_TEST_SRCS} ${INTEGRATION_TEST_DRIVERS_SRCS})
target_compile_options(${INTEGRATION_TEST_MODULE} PRIVATE -Wno-error)

get_property(SRC_INCLUDE_DIR GLOBAL PROPERTY "SRC_INCLUDE_DIR")
get_property(META_STORE_DIR GLOBAL PROPERTY "META_STORE_DIR")

target_include_directories(${INTEGRATION_TEST_MODULE} PRIVATE
        ${SRC_INCLUDE_DIR}
        ${gtest_INCLUDE_DIR}
        ${META_STORE_DIR}
)

add_subdirectory(stubs)
target_compile_options(${INTEGRATION_TEST_MODULE} PRIVATE "-fvisibility=hidden")
target_link_libraries(${INTEGRATION_TEST_MODULE} PRIVATE
        ${grpcpp_LIB}
        ${gpr_LIB}
        ${protobuf_LIB}
        ${etcdapi_LIB_A}
        ${gtest_LIB_A}
        ${gmock_LIB_A}
        ${litebus_ALL_LIB}
        meta_store_client
        meta_store_lib
        resource_view
        posix_pb
        register
        rpc
        pthread)

add_test(NAME ${INTEGRATION_TEST_MODULE} COMMAND ${INTEGRATION_TEST_MODULE} --gtest_filter=*.*)
