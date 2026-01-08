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

# 基于 VENDOR_DIR 变量为需要依赖三方件的cmake提供配置
message(STATUS "Config vendor with VENDOR_DIR: ${VENDOR_DIR}")
if (NOT EXISTS ${VENDOR_DIR})
    message(FATAL_ERROR "Vendor path ${VENDOR_DIR} is not exist. Please recover it manually.")
endif()

# 设置三方件 src 文件夹为 | VENDOR_SRC_DIR
set(VENDOR_SRC_DIR ${VENDOR_DIR}/src)
message(STATUS "VENDOR_SRC_DIR: ${VENDOR_SRC_DIR}")

# 设置三方件 install 文件夹为 | VENDOR_INSTALL_DIR
set(VENDOR_INSTALL_DIR ${VENDOR_DIR}/output)
message(STATUS "VENDOR_INSTALL_DIR: ${VENDOR_INSTALL_DIR}")

# 设置三方件 CMake 文件夹为 | VENDOR_CMAKE_DIR
set(VENDOR_CMAKE_DIR ${VENDOR_DIR}/cmake)
message(STATUS "VENDOR_CMAKE_DIR: ${VENDOR_CMAKE_DIR}")

# 设置三方件 Patche 文件夹为 | VENDOR_PATCHES_DIR
set(VENDOR_PATCHES_DIR ${VENDOR_DIR}/patches)
message(STATUS "VENDOR_PATCHES_DIR: ${VENDOR_PATCHES_DIR}")

# 加载并配置 CMake 的 ExternalProject 功能模块
include(ExternalProject)  # 该模块用于管理和构建外部依赖项目
set(EP_BUILD_DIR "${VENDOR_INSTALL_DIR}")
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
