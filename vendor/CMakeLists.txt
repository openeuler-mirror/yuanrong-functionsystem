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

# 设置编译项目基础为 vendor 文件夹 | VENDOR_SRC_DIR
set(VENDOR_SRC_DIR ${PROJECT_SOURCE_DIR}/src)
get_filename_component(ABSOLUTE_VENDOR_SRC_DIR ${VENDOR_SRC_DIR} ABSOLUTE)  # 设置为绝对路径
set(VENDOR_SRC_DIR ${ABSOLUTE_VENDOR_SRC_DIR})
message(STATUS "VENDOR_SRC_DIR: ${VENDOR_SRC_DIR}")

# 设置编译产物输出文件夹为 | VENDOR_INSTALL_DIR
set(VENDOR_INSTALL_DIR ${PROJECT_SOURCE_DIR}/output)
message(STATUS "VENDOR_INSTALL_DIR: ${VENDOR_INSTALL_DIR}")

# 设置三方件 Patche 文件夹为 | VENDOR_PATCHES_DIR
set(VENDOR_CMAKE_DIR ${PROJECT_SOURCE_DIR}/cmake)
message(STATUS "VENDOR_CMAKE_DIR: ${VENDOR_CMAKE_DIR}")

# 设置三方件 Patche 文件夹为 | VENDOR_PATCHES_DIR
set(VENDOR_PATCHES_DIR ${PROJECT_SOURCE_DIR}/patches)
message(STATUS "VENDOR_PATCHES_DIR: ${VENDOR_PATCHES_DIR}")

# 检查并设置 Linux 发行版类型 | LINUX_DISTRIBUTION
if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" OS_RELEASE_CONTENT REGEX "^ID=")
    string(REGEX REPLACE "^ID=\"?([^\"\n]+)\"?.*" "\\1" LINUX_DISTRIBUTION "${OS_RELEASE_CONTENT}")
else()
    set(LINUX_DISTRIBUTION "Unknown")
endif()
message(STATUS "LINUX_DISTRIBUTION: ${LINUX_DISTRIBUTION}")

# 加载并配置 CMake 的 ExternalProject 功能模块
include(ExternalProject)  # 该模块用于管理和构建外部依赖项目
set(EP_BUILD_DIR "${VENDOR_INSTALL_DIR}/${LINUX_DISTRIBUTION}")
set_property(DIRECTORY PROPERTY EP_BASE ${EP_BUILD_DIR})  # 设置构建基目录为 EP_BUILD_DIR
message(STATUS "EP_BUILD_DIR: ${EP_BUILD_DIR}")

# 设置三方件编译的多项编译参数
set(CODE_GENERATE_FLAGS "-fno-common -freg-struct-return -fstrong-eval-order")
set(OPTIMIZE_FLAGS "-ffunction-sections -fdata-sections")
set(COMPILE_SAFE_FLAGS "-fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(THIRDPARTY_COMMON_FLAGS "-O2 -fuse-ld=gold -pipe ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS} -DNDEBUG")

set(CMAKE_C_FLAGS_RELEASE "-std=gnu11 ${THIRDPARTY_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-std=c++14 ${THIRDPARTY_COMMON_FLAGS}")
set(THIRDPARTY_C_FLAGS "${CMAKE_C_FLAGS_RELEASE}")
set(THIRDPARTY_CXX_FLAGS "${CMAKE_CXX_FLAGS_RELEASE}")

set(LINK_SAFE_FLAGS "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -s")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINK_SAFE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie ${LINK_SAFE_FLAGS}")
set(THIRDPARTY_LINK_FLAGS "-Wl,--gc-sections -Wl,--build-id=none -Wl,-z,origin ${LINK_SAFE_FLAGS}")

# 创建三方件添加 Patch 文件的函数
function(PATCH_FOR_SOURCE WORK_DIR PATCH_FILES)
    message(STATUS "Add patche ${PATCH_FILES} to ${WORK_DIR}")
    set(PATCH_FILE "${WORK_DIR}/patch_applied")
    if(NOT EXISTS ${PATCH_FILE})
    set(INDEX 1)
    while(INDEX LESS ${ARGC})
        execute_process(COMMAND patch -p1 -i ${ARGV${INDEX}}
                WORKING_DIRECTORY ${WORK_DIR}
                RESULT_VARIABLE _RET)
        if(NOT _RET EQUAL "0")
            message("patch ${ARGV${INDEX}} for ${WORK_DIR} failed, result: ${_RET}")
        endif()
        math(EXPR INDEX "${INDEX}+1")
    endwhile()
    file(WRITE ${PATCH_FILE} "Patch applied")
    endif()
endfunction()


set(CMAKE_BUILD_TYPE Release)

# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})
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

if("${BUILD_TEST_THIRDPARTY}" STREQUAL "ON")
    include(gtest_1_10_0)
    include(gtest_1_12_1)
    include(curl)

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
    )
endif()


