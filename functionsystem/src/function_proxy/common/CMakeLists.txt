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
project(function_proxy_common)

add_library(function_proxy_common_lib STATIC "")
set_target_properties(function_proxy_common_lib PROPERTIES UNITY_BUILD ON)
target_compile_options(function_proxy_common_lib PRIVATE -fPIE)
target_include_directories(function_proxy_common_lib PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(flags)
add_subdirectory(posix_client)
add_subdirectory(posix_service)
add_subdirectory(posix_auth_interceptor)
add_subdirectory(communication)
add_subdirectory(data_view)
add_subdirectory(observer)
add_subdirectory(state_handler)
add_subdirectory(state_machine)
add_subdirectory(iam)
add_subdirectory(distribute_cache_client)
add_subdirectory(rate_limiter)
add_subdirectory(common_driver)

add_dependencies(function_proxy_common_lib service_json rpc meta_store_client
        common_flags)
target_link_libraries(function_proxy_common_lib aksk service_json rpc meta_store_client
        common_flags ${datasystem_LIB} busproxy_lib)