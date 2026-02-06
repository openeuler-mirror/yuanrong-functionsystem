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

project(kv_client)
add_library(kv_client STATIC "")
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} KV_CLIENT_SRCS)
add_dependencies(kv_client status)
target_sources(kv_client PRIVATE ${KV_CLIENT_SRCS})
target_link_libraries(kv_client PUBLIC
    status
    utils
    ${datasystem_LIB})
