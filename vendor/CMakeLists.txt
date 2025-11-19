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
set(CMAKE_BUILD_TYPE Release)

message(STATUS "Start to config vendor CMakeLists")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")

# 设置 vendor 文件夹路径 | VENDOR_DIR
set(VENDOR_DIR ${PROJECT_SOURCE_DIR})
get_filename_component(ABSOLUTE_VENDOR_DIR ${VENDOR_DIR} ABSOLUTE)  # 设置为绝对路径
set(VENDOR_DIR ${ABSOLUTE_VENDOR_DIR})
message(STATUS "VENDOR_DIR: ${VENDOR_DIR}")

# 设置三方件编译配置，导入公共参数
include(${PROJECT_SOURCE_DIR}/vendor_utils.cmake)

# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})
include(gtest_1_12_1)
include(gtest_1_10_0)
include(cjson)
include(openssl)
include(securec)
include(spdlog)
include(yaml)
include(zlib)
include(protobuf)
include(grpc)
include(etcd)
include(jemalloc)
include(curl)
include(obs)


add_custom_target(main
    DEPENDS
    gtest_1_12_1
    gtest_1_10_0
    etcd
    grpc
    curl
    cjson
    openssl
    jemalloc
    protobuf
    securec
    spdlog
    yaml
    zlib
    obs
)


