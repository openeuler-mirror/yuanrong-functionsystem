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

project(busproxy)

set_property(GLOBAL PROPERTY SRC_BUSPROXY_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")

include_directories(${CMAKE_CURRENT_LIST_DIR})

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/instance_proxy INSTANCE_PROXY_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/instance_view INSTANCE_VIEW_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/invocation_handler INVOCATION_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/registry REGISTRY_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/startup STARTUP_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/memory_monitor MONITOR_SRCS)

add_library(busproxy_lib STATIC "")
set_target_properties(busproxy_lib PROPERTIES UNITY_BUILD ON)
set_target_properties(busproxy_lib PROPERTIES UNITY_BUILD_BATCH_SIZE 9) # only 9 files, set in one batch
target_compile_options(busproxy_lib PRIVATE -fPIE)
target_sources(busproxy_lib PRIVATE ${INSTANCE_PROXY_SRCS} ${INSTANCE_VIEW_SRCS} ${INVOCATION_SRCS} ${REGISTRY_SRCS} ${STARTUP_SRCS} ${MONITOR_SRCS})
target_link_libraries(busproxy_lib ${LITEBUS_LIB} meta_store_client meta_store_adapter utils)
