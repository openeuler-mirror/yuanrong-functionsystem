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

set(FUNCTION_AGENT function_agent)
set(FUNCTION_AGENT_LIB function_agent_lib)

message(STATUS "Begin to configure function_agent")

set_property(GLOBAL PROPERTY FUNCTION_AGENT_DIR "${CMAKE_CURRENT_LIST_DIR}") # for ut
set_property(GLOBAL PROPERTY FUNCTION_AGENT_LIB ${FUNCTION_AGENT_LIB}) # for ut

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} FUNCTION_AGENT_SRCS)
list(REMOVE_ITEM FUNCTION_AGENT_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp") # exclude main
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/common FUNCTION_AGENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/flags FUNCTION_AGENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/code_deployer FUNCTION_AGENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/driver FUNCTION_AGENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/network FUNCTION_AGENT_SRCS)

add_library(${FUNCTION_AGENT_LIB} STATIC # for [function_agent_lib]
        ${FUNCTION_AGENT_SRCS})
set_target_properties(${FUNCTION_AGENT_LIB} PROPERTIES UNITY_BUILD ON)
target_compile_options(${FUNCTION_AGENT_LIB} PRIVATE -fPIE)

add_dependencies(${FUNCTION_AGENT_LIB} zlib rpc)
target_include_directories(${FUNCTION_AGENT_LIB} PUBLIC # for [function_agent_lib]
        ${CMAKE_CURRENT_LIST_DIR}
        ${zlib_INCLUDE_DIR}) # for module
target_link_libraries(${FUNCTION_AGENT_LIB} PUBLIC # for [function_agent_lib]
        ${eSDKOBS_LIB}
        ${zlib_LIB}
        ${zlib_MINIZIP_LIB}
        runtime_manager_lib)

add_executable(${FUNCTION_AGENT} main.cpp)
target_compile_options(${FUNCTION_AGENT} PRIVATE -fPIE)

target_link_libraries(${FUNCTION_AGENT} PRIVATE # after [add_executable]
        ${FUNCTION_AGENT_LIB}
)
add_dependencies(${FUNCTION_AGENT} posix_pb) # after [add_library, add_executable]

target_link_libraries(${FUNCTION_AGENT} PRIVATE rpc ${FUNCTION_AGENT_LIB})

INSTALL_TARGET(${FUNCTION_AGENT})