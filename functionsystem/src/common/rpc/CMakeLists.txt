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

project(rpc)

message(STATUS "Begin to configure rpc")
add_library(rpc STATIC "")

get_property(UTILS_INCLUDE GLOBAL PROPERTY "UTILS_INCLUDE")

include_directories(${UTILS_INCLUDE})
set_target_properties(rpc PROPERTIES UNITY_BUILD ON)
file(GLOB_RECURSE RPC_SRCS *.cpp)
target_sources(rpc PRIVATE ${RPC_SRCS})
target_compile_options(rpc PRIVATE "-fvisibility=hidden")
target_link_libraries(rpc PUBLIC
        ${zlib_LIB}
        ${protobuf_LIB}
        ${grpcpp_LIB}
        ${gpr_LIB}
        ${yrlogs_LIB}
        ${crypto_LIB}
        ${ssl_LIB}
        status
        posix_pb)