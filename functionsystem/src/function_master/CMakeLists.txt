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

set(EXECUTABLE_NAME function_master)

project(${EXECUTABLE_NAME})

message(STATUS "Begin to configure ${EXECUTABLE_NAME}")

add_executable(${EXECUTABLE_NAME} main.cpp)
target_compile_options(${EXECUTABLE_NAME} PRIVATE -fPIE)
include_directories(${META_STORE_CLIENT_INCLUDE_DIR})

add_subdirectory(common)
add_subdirectory(global_scheduler)
add_subdirectory(instance_manager)
add_subdirectory(scaler)
add_subdirectory(system_function_loader)
add_subdirectory(resource_group_manager)

get_property(META_STORE_DIR GLOBAL PROPERTY "META_STORE_DIR")

target_include_directories(${EXECUTABLE_NAME} PRIVATE
        ${META_STORE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/common
        ${CMAKE_CURRENT_LIST_DIR}/global_scheduler
        ${CMAKE_CURRENT_LIST_DIR}/instance_manager
        ${CMAKE_CURRENT_LIST_DIR}/scaler
        ${CMAKE_CURRENT_LIST_DIR}/system_function_loader
        ${CMAKE_CURRENT_LIST_DIR}/resource_group_manager)

add_dependencies(${EXECUTABLE_NAME} rpc meta_store_client leader utils heartbeat
        meta_store_lib hex)
get_property(PLUGIN_LIB GLOBAL PROPERTY "PLUGIN_LIB")
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        meta_store_client leader utils heartbeat
        meta_store_lib rpc hex
        ${ssl_LIB}
        ${litebus_ALL_LIB}
        ${LITEBUS_LIB}
        ${MASTER_COMMON_LIB}
        ${GLOBAL_SCHEDULER_LIB}
        ${INSTANCE_MANAGER_LIB}
        ${META_STORE_LIB}
        ${SCALER_LIB}
        ${SYSTEM_FUNCTION_LOADER_LIB}
        ${RESOURCE_GROUP_MANAGER_LIB}
        ${PLUGIN_LIB})

INSTALL_TARGET(${EXECUTABLE_NAME})
