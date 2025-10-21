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

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} POSIX_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/control_plane_client POSIX_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/data_plane_client POSIX_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/shared_client POSIX_CLIENT_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${POSIX_CLIENT_SRCS})

target_include_directories(function_proxy_common_lib PRIVATE ${CMAKE_CURRENT_LIST_DIR})