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
project(functionsystem LANGUAGES CXX C)

message(STATUS "PROJECT_SOURCE_DIR：${PROJECT_SOURCE_DIR}")
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "compile tools: ccache")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

include(GNUInstallDirs)
set(CMAKE_INSTALL_LIBDIR
        "lib"
        CACHE PATH "Installation directory name for libraries" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
        ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
        ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})
set(INSTALL_LIBDIR
        ${CMAKE_INSTALL_LIBDIR}
        CACHE PATH "Installation directory for libraries")
set(INSTALL_BINDIR
        ${CMAKE_INSTALL_BINDIR}
        CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDEDIR
        ${CMAKE_INSTALL_INCLUDEDIR}
        CACHE PATH "Installation directory for header files")

foreach (dir LIBDIR BINDIR INCLUDEDIR)
    file(TO_NATIVE_PATH ${CMAKE_INSTALL_PREFIX}/${INSTALL_${dir}} _install_dir)
    message(STATUS "Installing ${dir} components to ${_install_dir}")
    unset(_install_dir)
endforeach ()

# 编译选项
option(BUILD_GCOV "Enable functionsystem gcov output" OFF)
option(BUILD_LLT "Enable functionsystem unit test" OFF)
option(SANITIZERS "Enable Sanitizers tool to detect progress bugs" OFF)

# 引入三方件与配置
# 设置 vendor 文件夹路径 | VENDOR_DIR
set(VENDOR_DIR ${PROJECT_SOURCE_DIR}/../vendor)
message(STATUS "VENDOR_DIR: ${VENDOR_DIR}")
# 设置三方件编译配置，导入公共参数
include(${VENDOR_DIR}/vendor_utils.cmake)
# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})
include(securec)
include(openssl)
include(zlib)
include(protobuf)
include(grpc)
include(etcd)
include(spdlog)
include(yaml)
include(cjson)
include(jemalloc)
include(curl)
include(obs)
if (BUILD_LLT)
    include(gtest_1_10_0)
endif()
# 引入二方件编译的 cmake 文件
include(${CMAKE_CURRENT_LIST_DIR}/../common/logs/logs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../common/litebus/litebus.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../common/metrics/metrics.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/datasystem.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/funcs.cmake)

# 设置 litebus 相关库变量
set(litebus_ALL_LIB ${litebus_LIB} ${securec_LIB} ${yrlogs_LIB} pthread)

# ---------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)

# Compile flags
set(WARN_FLAGS "-Wall -Werror -Wtrampolines -Wformat=2 -Wdate-time -Wcast-qual -Wcast-align -Wunused -Wno-deprecated-declarations -Wno-maybe-uninitialized -Wno-float-equal")
set(CXX_WANR_FLAGS "${WARN_FLAGS} -Wno-non-virtual-dtor -Wdelete-non-virtual-dtor")
set(C_WANR_FLAGS "${WARN_FLAGS} -Wstrict-prototypes")
set(LANGUAGE_FLAGS "-fsigned-char -funsigned-char")
set(CODE_GENERATE_FLAGS "-freg-struct-return -fstrong-eval-order")
set(OPTIMIZE_FLAGS "-fno-omit-frame-pointer")
set(COMPILE_SAFE_FLAGS "-fPIC -fstack-protector-strong")
if (CMAKE_BUILD_TYPE STREQUAL Release)
    set(COMPILE_SAFE_FLAGS "${COMPILE_SAFE_FLAGS} -D_FORTIFY_SOURCE=2")
endif ()

function(set_linker_flags)
    # Check if it is CentOS
    if(EXISTS "/etc/os-release")
        file(READ "/etc/os-release" OS_RELEASE_CONTENT)
        string(REGEX MATCH "ID=\"([A-Za-z]+)\"" OS_ID_MATCH "${OS_RELEASE_CONTENT}")
        if(OS_ID_MATCH)
            string(REGEX REPLACE "ID=\"([A-Za-z]+)\"" "\\1" OS_ID "${OS_ID_MATCH}")
            if(OS_ID STREQUAL "centos")
                set(IS_CENTOS TRUE)
            endif()
        endif()
    endif()

    # To prevent execution from stalling, avoid using all available CPU cores.
    math(EXPR THREAD_NUM_MINUS_ONE "${BUILD_THREAD_NUM} - 1")
    find_program(LINKER_LLD NAMES ld.lld lld)
    if(LINKER_LLD)
        set(CMAKE_CXX_LD_FLAGS "-fuse-ld=lld -Wl,--threads=${THREAD_NUM_MINUS_ONE}")
        set(CMAKE_C_LD_FLAGS "-fuse-ld=lld -Wl,--threads=${THREAD_NUM_MINUS_ONE}")
    elseif(CMAKE_LINKER MATCHES "ld.gold")
        if(IS_CENTOS)
            # In the CentOS compile container environment, the ld.gold binary was compiled without multithreading support,
            # To eliminate warnings, multi-threading linking options are not enabled.
            set(CMAKE_CXX_LD_FLAGS "-fuse-ld=gold")
            set(CMAKE_C_LD_FLAGS "-fuse-ld=gold")
        else()
            set(CMAKE_CXX_LD_FLAGS "-fuse-ld=gold -Wl,--threads -Wl,--thread-count=${THREAD_NUM_MINUS_ONE}")
            set(CMAKE_C_LD_FLAGS "-fuse-ld=gold -Wl,--threads -Wl,--thread-count=${THREAD_NUM_MINUS_ONE}")
        endif()
    else()
        set(CMAKE_CXX_LD_FLAGS "-fuse-ld=gold -Wl,--threads -Wl,--thread-count=${THREAD_NUM_MINUS_ONE}")
        set(CMAKE_C_LD_FLAGS "-fuse-ld=gold -Wl,--threads -Wl,--thread-count=${THREAD_NUM_MINUS_ONE}")
    endif()
endfunction()

set_linker_flags()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_LD_FLAGS} -pipe -lcap ${CXX_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3 -ftrapv -fstack-check")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wextra -Wno-unused-parameter -Wno-float-equal -DNDEBUG")
set(CMAKE_C_FLAGS "${CMAKE_C_LD_FLAGS} -pipe ${C_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -ftrapv -fstack-check")
set(CMAKE_C_FLAGS_RELEASE "-O2 -Wextra -Wno-float-equal -DNDEBUG")

# Link flags
set(LINK_SAFE_FLAGS "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack")
set(LINK_COMMON_FLASG "-Wl,--no-as-needed ${LINK_SAFE_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${LINK_COMMON_FLASG}")
set(CMAKE_EXE_LINKER_FLAGS "")
if (CMAKE_BUILD_TYPE STREQUAL Release)
    set(CMAKE_EXE_LINKER_FLAGS "-pie ${LINK_COMMON_FLASG}")
endif ()


# compiler flags that are status across debug/release builds:
if (SANITIZERS)
    # use Google sanitizers to detect memory leak and thread race.
    string(TOUPPER ${SANITIZERS} SANITIZERS)
    if (${SANITIZERS} STREQUAL "ADDRESS")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g3 -lasan")
    elseif(${SANITIZERS} STREQUAL "THREAD")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g3 -lasan")
    elseif(${SANITIZERS} STREQUAL "MEMORY")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory -fPIE -pie -fno-omit-frame-pointer -g3 -lasan")
    else()
        message(FATAL_ERROR "Unrecognized sanitizers mode: ${SANITIZERS}")
    endif()
endif()

if(BUILD_GCOV)
    message(STATUS "GCOV is enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 --coverage -fprofile-arcs -ftest-coverage")
endif()

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-D__DEBUG__)
endif ()

set(BUILD_VERSION ${BUILD_VERSION})

set(GIT_HASH "Unknown")
set(GIT_BRANCH_NAME "Unknown")
if ("${BUILD_LLT}" STREQUAL "OFF")
    get_git_hash(GIT_HASH)
    get_git_branch(GIT_BRANCH_NAME)
endif ()
message(STATUS "GIT COMMIT: ${GIT_HASH}; BRANCH: ${GIT_BRANCH_NAME}")

get_property(META_STORE_CLIENT_INCLUDE_DIR GLOBAL PROPERTY "META_STORE_CLIENT_INCLUDE_DIR")
include_directories(${META_STORE_CLIENT_INCLUDE_DIR})
add_subdirectory(src)
add_subdirectory(tests)