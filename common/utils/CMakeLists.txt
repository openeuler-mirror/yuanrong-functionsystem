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

project(utils)

set_property(GLOBAL PROPERTY UTILS_INCLUDE "${CMAKE_CURRENT_LIST_DIR}")
include_directories(${CMAKE_CURRENT_LIST_DIR}/metadata)
include_directories(${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(proto)
add_subdirectory(status)
add_subdirectory(heartbeat)
add_subdirectory(metrics)
add_subdirectory(rpc)
add_subdirectory(common_flags)
add_subdirectory(http)
