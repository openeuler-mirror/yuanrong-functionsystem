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

project(runtime_manager_lib)

message(STATUS "Begin to configure runtime_manager")

set_property(GLOBAL PROPERTY SRC_RUNTIME_MANAGER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} RUNTIME_MANAGER_SRCS)
list(REMOVE_ITEM RUNTIME_MANAGER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp") # exclude main
include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(runtime_manager_lib STATIC ${RUNTIME_MANAGER_SRCS})
set_target_properties(runtime_manager_lib PROPERTIES UNITY_BUILD ON)
target_compile_options(runtime_manager_lib PRIVATE -fPIE)

add_subdirectory(executor)
add_subdirectory(metrics)
add_subdirectory(healthcheck)
add_subdirectory(manager)
add_subdirectory(config)
add_subdirectory(utils)
add_subdirectory(port)
add_subdirectory(driver)
add_subdirectory(log)
add_subdirectory(debug)
add_subdirectory(virtual_env_manager)

target_link_libraries(runtime_manager_lib ${litebus_ALL_LIB}
        ${yaml_LIB}
	aksk
        register common_flags
        http file_monitor network
        utils metrics_adapter resource_view)

add_executable(runtime_manager main.cpp)
target_compile_options(runtime_manager PRIVATE -fPIE)

target_link_libraries(runtime_manager PRIVATE runtime_manager_lib)

INSTALL_TARGET(runtime_manager)