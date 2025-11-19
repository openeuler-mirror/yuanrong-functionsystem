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

project(function_proxy)

message(STATUS "Begin to configure function_proxy")

set_property(GLOBAL PROPERTY SRC_FUNCTION_PROXY_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")

include_directories(${CMAKE_CURRENT_LIST_DIR})
include_directories(${CMAKE_CURRENT_LIST_DIR}/busproxy)
include_directories(${CMAKE_CURRENT_LIST_DIR}/local_scheduler)

add_subdirectory(busproxy)
add_subdirectory(local_scheduler)
add_subdirectory(common)

add_executable(function_proxy main.cpp)
target_compile_options(function_proxy PRIVATE -fPIE)

target_compile_options(function_proxy PRIVATE "-fvisibility=hidden")
get_property(PLUGIN_LIB GLOBAL PROPERTY "PLUGIN_LIB")
target_link_libraries(function_proxy PRIVATE
        ${crypto_LIB} ${ssl_LIB}
        ${protobuf_LIB} ${grpcpp_LIB} ${gpr_LIB}
        ${litebus_ALL_LIB}
        busproxy_lib local_scheduler_lib
        function_proxy_common_lib utils
        resource_lock
        ${PLUGIN_LIB})

INSTALL_TARGET(function_proxy)