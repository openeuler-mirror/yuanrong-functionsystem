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
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} RESOURCE_GROUP_MANAGER_SRCS)

set(RESOURCE_GROUP_MANAGER_LIB resource_group_manager_lib) # for this directory
set(RESOURCE_GROUP_MANAGER_LIB ${RESOURCE_GROUP_MANAGER_LIB} PARENT_SCOPE) # for function_master directory

set_property(GLOBAL PROPERTY RESOURCE_GROUP_MANAGER_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_property(GLOBAL PROPERTY RESOURCE_GROUP_MANAGER_LIB ${RESOURCE_GROUP_MANAGER_LIB})

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(${RESOURCE_GROUP_MANAGER_LIB} STATIC
        ${RESOURCE_GROUP_MANAGER_SRCS})
target_compile_options(${RESOURCE_GROUP_MANAGER_LIB} PRIVATE -fPIE)

target_include_directories(${RESOURCE_GROUP_MANAGER_LIB} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${RESOURCE_GROUP_MANAGER_LIB} PUBLIC meta_store_client posix_pb)