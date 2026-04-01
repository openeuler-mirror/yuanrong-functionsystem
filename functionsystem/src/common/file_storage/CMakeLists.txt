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

project(file_storage)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} FILE_STORAGE_SRCS)

add_library(file_storage OBJECT ${FILE_STORAGE_SRCS})
target_compile_options(file_storage PRIVATE -fPIE)

target_include_directories(file_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(file_storage status kv_client)
target_link_libraries(file_storage PUBLIC
    status
    kv_client
    ${datasystem_LIB})
