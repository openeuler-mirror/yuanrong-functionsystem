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

project(domain_scheduler)

message(STATUS "Begin to configure domain_scheduler")

set_property(GLOBAL PROPERTY SRC_DOMAIN_SCHEDULER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")

add_library(domain_scheduler_lib STATIC domain_scheduler_launcher.cpp)
set_target_properties(domain_scheduler_lib PROPERTIES UNITY_BUILD ON)
target_compile_options(domain_scheduler_lib PRIVATE -fPIE)

add_subdirectory(startup)
add_subdirectory(domain_scheduler_service)
add_subdirectory(underlayer_scheduler_manager)
add_subdirectory(instance_control)
add_subdirectory(flags)
add_subdirectory(domain_group_control)
add_dependencies(domain_scheduler_lib
        meta_store_client explorer scheduler common_flags utils heartbeat kube_client aksk)

target_link_libraries(domain_scheduler_lib PUBLIC
        meta_store_client explorer scheduler common_flags utils heartbeat kube_client aksk)

target_include_directories(domain_scheduler_lib PRIVATE ${CMAKE_CURRENT_LIST_DIR})

get_property(PLUGIN_LIB GLOBAL PROPERTY "PLUGIN_LIB")
add_executable(domain_scheduler main.cpp)
target_compile_options(domain_scheduler PRIVATE -fPIE)
target_compile_options(domain_scheduler PRIVATE "-fvisibility=hidden")
target_link_libraries(domain_scheduler PRIVATE domain_scheduler_lib
        ${protobuf_LIB}
        ${grpcpp_LIB}
        ${gpr_LIB}
        ${litebus_ALL_LIB}
        crypto rpc
        ${PLUGIN_LIB})

INSTALL_TARGET(domain_scheduler)