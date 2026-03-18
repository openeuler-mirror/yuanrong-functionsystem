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

# In Bazel build mode, several vendor components are either built from source
# by Bazel itself or are not referenced by any Bazel target, so they can be
# skipped to reduce build time.
#
# Skipped when BAZEL_MODE=ON:
#   gtest_1_12_1 / gtest_1_10_0  — Bazel uses @gtest http_archive for tests
#   yaml                          — Bazel uses @yaml-cpp http_archive
#   jemalloc                      — not referenced by any Bazel BUILD target
#   cjson                         — not referenced by any Bazel BUILD target
#   opentelemetry                 — Bazel builds from source via @opentelemetry_cpp
#   protobuf, grpc                — Bazel builds from source via @com_google_protobuf/@com_github_grpc_grpc
#   etcd                          — Bazel builds etcdapi from source via //vendor/etcdapi:etcdapi
#
# Always built (Bazel still needs CMake output):
#   openssl                       — dep for curl/obs CMake builds
#   securec                       — dep for obs CMake build
#   spdlog                        — dep for obs CMake build (obs.cmake: DEPENDS ... spdlog)
#   zlib                          — @minizip_sdk points to vendor/output/Install/zlib/
#   curl                          — @curl_sdk prebuilt .so
#   obs                           — @obs_sdk prebuilt .so
#
# Note: protoc + grpc_cpp_plugin for proto generation now come from Bazel-built
#   bazel-bin/external/com_google_protobuf/protoc
#   bazel-bin/external/com_github_grpc_grpc/src/compiler/grpc_cpp_plugin
option(BAZEL_MODE "Build only vendor components required by Bazel build mode" OFF)
message(STATUS "BAZEL_MODE: ${BAZEL_MODE}")

# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})

include(openssl)
include(securec)
include(spdlog)
include(zlib)
include(curl)
include(obs)

if (NOT BAZEL_MODE)
    include(gtest_1_12_1)
    include(gtest_1_10_0)
    include(cjson)
    include(yaml)
    include(jemalloc)
    include(protobuf)
    include(grpc)
    include(etcd)
    include(opentelemetry)
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
else()
    add_custom_target(main
        DEPENDS
        curl
        openssl
        securec
        spdlog
        zlib
        obs
    )
endif()
