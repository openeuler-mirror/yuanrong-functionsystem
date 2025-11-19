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

project(meta_store_client)

message(STATUS "Begin to configure meta_store_client")
set_property(GLOBAL PROPERTY META_STORE_CLIENT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")
get_property(UTILS_INCLUDE GLOBAL PROPERTY "UTILS_INCLUDE")

include_directories(${UTILS_INCLUDE})
include_directories(${UTILS_INCLUDE}/metadata)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_storage_accessor)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_monitor)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_client)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_client/election)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_client/key_value)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_client/lease)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_client/maintenance)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include/meta_store_client/utils)

add_library(meta_store_client STATIC "")
set_target_properties(meta_store_client PROPERTIES UNITY_BUILD ON)

file(GLOB_RECURSE META_STORE_CLIENT_SRCS src/*.cpp)

add_dependencies(meta_store_client metrics_adapter status etcdapi heartbeat)
target_sources(meta_store_client PRIVATE ${META_STORE_CLIENT_SRCS})
target_link_libraries(meta_store_client PUBLIC metrics_adapter ${etcdapi_LIB_A} status heartbeat)