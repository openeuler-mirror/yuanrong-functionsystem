# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SNAP_MANAGER_SRCS)

set(SNAP_MANAGER_LIB snap_manager_lib)
set(SNAP_MANAGER_LIB ${SNAP_MANAGER_LIB} PARENT_SCOPE)

set_property(GLOBAL PROPERTY SNAP_MANAGER_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_property(GLOBAL PROPERTY SNAP_MANAGER_LIB ${SNAP_MANAGER_LIB})

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(${SNAP_MANAGER_LIB} STATIC
        ${SNAP_MANAGER_SRCS})
target_compile_options(${SNAP_MANAGER_LIB} PRIVATE -fPIE)

target_include_directories(${SNAP_MANAGER_LIB} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})

add_dependencies(${SNAP_MANAGER_LIB} meta_store_client leader)
target_link_libraries(${SNAP_MANAGER_LIB} PUBLIC
        meta_store_client
        leader)
