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

# ---------------------------------------------------------------------------------------
# YuanRong yrlogs project
# ---------------------------------------------------------------------------------------

project(yrlogs LANGUAGES CXX C)
message(STATUS "project source dir of yrlogs: ${PROJECT_SOURCE_DIR}")

set(LOGS_INSTALL_LIBDIR "lib")
set(CMAKE_INSTALL_LIBDIR ${LOGS_INSTALL_LIBDIR})
set(INSTALL_LIBDIR ${LOGS_INSTALL_LIBDIR})
set(CMAKE_INSTALL_INCLUDEDIR "include")

# build shared option
option(LOGS_BUILD_SHARED "build shared library" ON)

# install options
option(LOGS_INSTALL "generate the install target" ON)

# test options
option(LOGS_BUILD_TEST "build test target" OFF)

# build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Set logs build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Set logs build config path: " ${BUILD_CONFIG_DIR})

# 引入三方件与配置
# 设置 vendor 文件夹路径 | VENDOR_DIR
set(VENDOR_DIR ${CMAKE_CURRENT_LIST_DIR}/../../vendor)
message(STATUS "VENDOR_DIR: ${VENDOR_DIR}")
# 设置三方件编译配置，导入公共参数
include(${VENDOR_DIR}/vendor_utils.cmake)
# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})
include(zlib)
include(spdlog)
include(cjson)

if (LOGS_BUILD_TEST)
    include(gtest_1_12_1)
endif()
message(STATUS "Config vendor cmake files done")
# ---------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)

set(WARN_FLAGS
    "-Wall -Werror -Wextra -Wconversion -Wno-float-equal -Wno-stringop-overflow -Wtrampolines -Wformat=2 -Wdate-time -Wcast-qual -Wcast-align -Wunused"
)
set(CXX_WANR_FLAGS
    "${WARN_FLAGS} -Woverloaded-virtual -Wnon-virtual-dtor -Wdelete-non-virtual-dtor"
)
set(C_WANR_FLAGS "${WARN_FLAGS} -Wstrict-prototypes")
set(LANGUAGE_FLAGS "-fsigned-char -funsigned-char")
set(CODE_GENERATE_FLAGS "-fno-common -freg-struct-return -fstrong-eval-order")
set(OPTIMIZE_FLAGS "-fno-omit-frame-pointer")
set(COMPILE_SAFE_FLAGS "-fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS
    "-fuse-ld=gold -pipe ${CXX_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}"
)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -ftrapv -fstack-check")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS
    "-fuse-ld=gold -pipe ${C_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}"
)
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -ftrapv -fstack-check")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

set(LINK_SAFE_FLAGS "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -s")
set(LINK_COMMON_FLASG "-rdynamic -Wl,--no-as-needed ${LINK_SAFE_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${LINK_COMMON_FLASG}")
set(CMAKE_EXE_LINKER_FLAGS "-pie ${LINK_COMMON_FLASG}")

if(BUILD_GCOV)
  message(STATUS "GCOV is enabled")
  set(CMAKE_CXX_FLAGS
          "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
  set(CMAKE_C_FLAGS
          "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
endif()

add_library(observability-logs-headers INTERFACE)
target_include_directories(
  observability-logs-headers
  INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/src/sdk LOGS_SRCS)

if(LOGS_BUILD_SHARED OR BUILD_SHARED_LIBS)
  add_library(yrlogs SHARED ${LOGS_SRCS})
else()
  add_library(yrlogs STATIC ${LOGS_SRCS})
endif()

add_library(yrlogs::yrlogs ALIAS yrlogs)

add_dependencies(yrlogs spdlog cjson zlib)

target_include_directories(
  yrlogs PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>")

target_link_libraries(yrlogs PUBLIC ${spdlog_LIB} ${zlib_LIB} pthread)

if(LOGS_BUILD_TEST)
  message(STATUS "generating tests")
  add_subdirectory(test)
endif()

# ---------------------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------------------
if(LOGS_INSTALL)
  message(STATUS "generating install")
  # ---------------------------------------------------------------------------------------
  # Include files
  # ---------------------------------------------------------------------------------------
  install(
    TARGETS observability-logs-headers
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(
    DIRECTORY include/logs
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.h")
  install(DIRECTORY ${spdlog_INCLUDE_DIR}/
          DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(
    TARGETS yrlogs
    EXPORT yrlogs
    LIBRARY DESTINATION ${LOGS_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${LOGS_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
