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

set(PROTO_SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../proto")
set(GRPC_PROTO_SRCS "${PROTO_SRC_PATH}/posix/common.proto"
        "${PROTO_SRC_PATH}/posix/core_service.proto"
        "${PROTO_SRC_PATH}/posix/runtime_rpc.proto"
        "${PROTO_SRC_PATH}/posix/runtime_service.proto"
        "${PROTO_SRC_PATH}/posix/affinity.proto"
        "${PROTO_SRC_PATH}/posix/inner_service.proto"
        "${PROTO_SRC_PATH}/posix/bus_service.proto"
        "${PROTO_SRC_PATH}/posix/message.proto"
        "${PROTO_SRC_PATH}/posix/resource.proto"
        "${PROTO_SRC_PATH}/posix/bus_adapter.proto"
        "${PROTO_SRC_PATH}/posix/agent_plugin.proto"
        "${PROTO_SRC_PATH}/posix/runtime_launcher_interface.proto"
        "${PROTO_SRC_PATH}/posix/exec_service.proto")
GENERATE_GRPC_CPP(GRPCPB_SRCS GRPCPB_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/pb/posix
        PROTO_FILES  ${GRPC_PROTO_SRCS}
        SOURCE_ROOT  ${PROTO_SRC_PATH}/posix)
include_directories(${CMAKE_CURRENT_LIST_DIR}/pb)
add_library(posix_pb STATIC ${GRPCPB_SRCS})
add_dependencies(posix_pb protobuf grpc)
target_compile_options(posix_pb PRIVATE "-fvisibility=hidden")
target_link_libraries(posix_pb PUBLIC
        ${protobuf_LIB} ${grpcpp_LIB} ${gpr_LIB})