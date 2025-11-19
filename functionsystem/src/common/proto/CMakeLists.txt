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

set(GRPC_PROTO_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/posix/common.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/core_service.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/runtime_rpc.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/runtime_service.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/affinity.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/inner_service.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/bus_service.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/message.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/resource.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/posix/bus_adapter.proto")
GENERATE_GRPC_CPP(GRPCPB_SRCS GRPCPB_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/pb/posix
        PROTO_FILES  ${GRPC_PROTO_SRCS}
        SOURCE_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/posix)
include_directories(${CMAKE_CURRENT_LIST_DIR}/pb)
add_library(posix_pb STATIC ${GRPCPB_SRCS})
set_target_properties(posix_pb PROPERTIES UNITY_BUILD ON)
set_target_properties(posix_pb PROPERTIES UNITY_BUILD_BATCH_SIZE 2)
add_dependencies(posix_pb protobuf grpc)
target_compile_options(posix_pb PRIVATE "-fvisibility=hidden")
target_link_libraries(posix_pb PUBLIC
        ${protobuf_LIB} ${grpcpp_LIB} ${gpr_LIB})