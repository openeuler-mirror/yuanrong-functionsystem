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
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} META_STORE_SRCS)

set(META_STORE_LIB meta_store_lib) # for this directory
set(META_STORE_LIB ${META_STORE_LIB} PARENT_SCOPE) # for function_master directory
get_property(META_STORE_CLIENT_INCLUDE_DIR GLOBAL PROPERTY "META_STORE_CLIENT_INCLUDE_DIR")
get_property(UTILS_INCLUDE GLOBAL PROPERTY "UTILS_INCLUDE")

set_property(GLOBAL PROPERTY META_STORE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_property(GLOBAL PROPERTY META_STORE_LIB ${META_STORE_LIB})

include_directories(${UTILS_INCLUDE})
include_directories(${UTILS_INCLUDE}/metadata)
include_directories(${META_STORE_CLIENT_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(${META_STORE_LIB} STATIC
        ${META_STORE_SRCS})
set_target_properties(${META_STORE_LIB} PROPERTIES UNITY_BUILD ON)
target_compile_options(${META_STORE_LIB} PRIVATE -fPIE)

add_dependencies(${META_STORE_LIB} meta_store_client http explorer)

target_include_directories(${META_STORE_LIB} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${META_STORE_LIB} PUBLIC meta_store_client http explorer)