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

# This attribute is used by the LLT. Because test and src belong to different directory tree.
set_property(GLOBAL PROPERTY SRC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")
if ("${FUNCTION_SYSTEM_BUILD_TIME_TRACE}" STREQUAL "ON")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(meta_store/client/cpp ${CMAKE_CURRENT_BINARY_DIR}/meta_store_client)
add_subdirectory(meta_store/server ${CMAKE_CURRENT_BINARY_DIR}/meta_store_server)
get_property(META_STORE_CLIENT_INCLUDE_DIR GLOBAL PROPERTY "META_STORE_CLIENT_INCLUDE_DIR")
include_directories(${META_STORE_CLIENT_INCLUDE_DIR})
add_subdirectory(common)
add_subdirectory(function_master)
add_subdirectory(domain_scheduler)
add_subdirectory(runtime_manager)
add_subdirectory(function_proxy)
add_subdirectory(function_agent)
add_subdirectory(iam_server)
