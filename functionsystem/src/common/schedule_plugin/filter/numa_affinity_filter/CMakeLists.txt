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

project(numa_affinity_filter)

add_library(numa_affinity_filter STATIC "")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SRCS)

add_dependencies(numa_affinity_filter schedule_plugin_common)

target_sources(numa_affinity_filter PRIVATE ${SRCS})

target_compile_options(numa_affinity_filter PRIVATE "-fvisibility=hidden")

target_link_libraries(numa_affinity_filter PUBLIC schedule_plugin_common ${protobuf_LIB} utils)
