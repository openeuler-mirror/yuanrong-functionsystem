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

project(common_flags)

message(STATUS "Begin to configure common_flags")
add_library(common_flags STATIC "")
get_property(UTILS_INCLUDE GLOBAL PROPERTY "UTILS_INCLUDE")

include_directories(${UTILS_INCLUDE})
set_target_properties(common_flags PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} COMMON_FLAGS_SRCS)
target_sources(common_flags PRIVATE ${COMMON_FLAGS_SRCS})
target_link_libraries(common_flags PUBLIC ${litebus_ALL_LIB})