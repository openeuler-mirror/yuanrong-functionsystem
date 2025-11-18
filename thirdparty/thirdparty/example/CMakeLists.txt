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

cmake_minimum_required(VERSION 3.16.1)
project(build-example LANGUAGES CXX C)

message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")

if ("${BUILD_CONFIG_DIR}" STREQUAL "")
message(STATUS "use default thirdparty config dir")
    set(BUILD_CONFIG_DIR ${PROJECT_SOURCE_DIR}/../../)
endif()

message(STATUS "BUILD_CONFIG_DIR is ${BUILD_CONFIG_DIR}")

set(THIRDPARTY_SRC_DIR "${BUILD_CONFIG_DIR}/../vendor")

set(CMAKE_BUILD_TYPE Release)

list(APPEND CMAKE_MODULE_PATH ${BUILD_CONFIG_DIR}/thirdparty/cmake)

include(third_utils)

include(cjson)
include(openssl)
include(securec)
include(spdlog)
include(yaml)
include(zlib)
include(curl)
include(obs)
include(protobuf)
include(grpc)
include(etcd)
include(jemalloc)

if("${BUILD_TEST_THIRDPARTY}" STREQUAL "ON")
    include(gtest_1_10_0)
    include(gtest_1_12_1)

    add_custom_target(main
        DEPENDS
        etcd
        grpc
        curl
        gtest_1_10_0
        gtest_1_12_1
        cjson
        openssl
        protobuf
        securec
        spdlog
        yaml
        zlib
        jemalloc
        obs
    )
else()
    add_custom_target(main
        DEPENDS
        etcd
        grpc
        cjson
        openssl
        protobuf
        securec
        spdlog
        yaml
        zlib
        jemalloc
        curl
        obs
    )
endif()


