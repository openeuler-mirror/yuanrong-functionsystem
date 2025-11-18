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

project(service_json)
add_library(service_json STATIC "")
set_target_properties(service_json PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SERVICE_JSON_SRCS)
add_dependencies(service_json posix_pb resource_view)
target_sources(service_json PRIVATE ${SERVICE_JSON_SRCS})
target_link_libraries(service_json PUBLIC posix_pb resource_view scheduler_topology)