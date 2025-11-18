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

project(aksk)

message(STATUS "Begin to configure aksk")
add_library(aksk STATIC "")
get_property(UTILS_INCLUDE GLOBAL PROPERTY "UTILS_INCLUDE")

include_directories(${UTILS_INCLUDE})
set_target_properties(aksk PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} AKSK_SRCS)
add_dependencies(aksk hex http)
target_sources(aksk PRIVATE ${AKSK_SRCS})
target_link_libraries(aksk PUBLIC hex http ${litebus_ALL_LIB})