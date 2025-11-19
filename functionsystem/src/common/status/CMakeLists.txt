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

project(status)

message(STATUS "Begin to configure status")
add_library(status STATIC "")
get_property(UTILS_INCLUDE GLOBAL PROPERTY "UTILS_INCLUDE")

include_directories(${UTILS_INCLUDE})
set_target_properties(status PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} STATUS_SRCS)
add_dependencies(status posix_pb)
target_sources(status PRIVATE ${STATUS_SRCS})
target_link_libraries(status PUBLIC posix_pb ${yrlogs_LIB})