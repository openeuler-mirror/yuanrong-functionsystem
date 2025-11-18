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

set(etcd_dir ${VENDOR_SRC_DIR}/etcd)
set(gogo_protobuf_dir ${VENDOR_SRC_DIR}/gogo-protobuf)
set(google_common_api_dir ${VENDOR_SRC_DIR}/grpc-gateway/third_party/googleapis)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/etcdapi")
if (NOT EXISTS ${HISTORY_INSTALLLED})

EXTERNALPROJECT_ADD(etcd_proto
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        CMAKE_ARGS -DOPENSSL_ROOT_DIR=${openssl_ROOT} -DOPENSSL_LIBRARIES=${openssl_LIB_DIR}
)

ExternalProject_Get_Property(etcd_proto SOURCE_DIR)
message("source dir of etcd_pb: ${SOURCE_DIR}")

set(etcd_proto_SRC ${SOURCE_DIR})

file(COPY ${etcd_dir}/api/authpb/auth.proto DESTINATION
        ${etcd_proto_SRC}/etcd/api/authpb/)
file(COPY ${etcd_dir}/api/etcdserverpb/etcdserver.proto DESTINATION
        ${etcd_proto_SRC}/etcd/api/etcdserverpb/)
file(COPY ${etcd_dir}/api/etcdserverpb/rpc.proto DESTINATION
        ${etcd_proto_SRC}/etcd/api/etcdserverpb/)
file(COPY ${etcd_dir}/api/mvccpb/kv.proto DESTINATION
        ${etcd_proto_SRC}/etcd/api/mvccpb/)
file(COPY ${etcd_dir}/server/etcdserver/api/v3election/v3electionpb/v3election.proto DESTINATION
        ${etcd_proto_SRC}/etcd/server/etcdserver/api/v3election/v3electionpb/)
file(COPY ${etcd_dir}/server/etcdserver/api/v3lock/v3lockpb/v3lock.proto DESTINATION
        ${etcd_proto_SRC}/etcd/server/etcdserver/api/v3lock/v3lockpb/)
file(COPY ${gogo_protobuf_dir}/gogoproto/gogo.proto DESTINATION
        ${etcd_proto_SRC}/gogoproto/)
file(COPY ${google_common_api_dir}/google/api/annotations.proto DESTINATION
        ${etcd_proto_SRC}/google/api)
file(COPY ${google_common_api_dir}/google/api/http.proto DESTINATION
        ${etcd_proto_SRC}/google/api/)

set(PROTO_SRCS
        ${etcd_proto_SRC}/etcd/api/authpb/auth.proto
        ${etcd_proto_SRC}/etcd/api/etcdserverpb/etcdserver.proto
        ${etcd_proto_SRC}/etcd/api/etcdserverpb/rpc.proto
        ${etcd_proto_SRC}/etcd/api/mvccpb/kv.proto
        ${etcd_proto_SRC}/etcd/server/etcdserver/api/v3election/v3electionpb/v3election.proto
        ${etcd_proto_SRC}/etcd/server/etcdserver/api/v3lock/v3lockpb/v3lock.proto
        ${etcd_proto_SRC}/gogoproto/gogo.proto
        ${etcd_proto_SRC}/google/api/annotations.proto
        ${etcd_proto_SRC}/google/api/http.proto
)

set (PROTO_GRPC_SRCS
        ${etcd_proto_SRC}/etcd/api/etcdserverpb/rpc.proto
        ${etcd_proto_SRC}/etcd/server/etcdserver/api/v3election/v3electionpb/v3election.proto
        ${etcd_proto_SRC}/etcd/server/etcdserver/api/v3lock/v3lockpb/v3lock.proto
)

# todo
set(etcdapi_CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -Dprotobuf_ROOT=${protobuf_ROOT}
        -Dutf8_range_DIR:PATH=${utf8_range_PKG_PATH}
        -Dc-ares_ROOT=${c-ares_ROOT}
        -Dabsl_ROOT=${absl_ROOT}
        -Dre2_ROOT=${re2_ROOT}
        -Dgrpc_ROOT=${grpc_ROOT}
        -DOPENSSL_ROOT_DIR=${openssl_ROOT}
        -DOPENSSL_LIBRARIES=${openssl_LIB_DIR}
        -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
)

message(STATUS "VENDOR_INSTALL_DIR: ${VENDOR_INSTALL_DIR}/")

EXTERNALPROJECT_ADD(etcdapi
        DOWNLOAD_COMMAND
                cd ${etcd_proto_SRC} &&
                LD_LIBRARY_PATH=${grpc_LIB_DIR}:${protobuf_LIB_DIR} ${protobuf_ROOT}/bin/protoc --proto_path=${etcd_proto_SRC}/ --cpp_out=<SOURCE_DIR> ${PROTO_SRCS} &&
                LD_LIBRARY_PATH=${grpc_LIB_DIR}:${protobuf_LIB_DIR} ${protobuf_ROOT}/bin/protoc --proto_path=${etcd_proto_SRC}/ --grpc_out=<SOURCE_DIR> ${PROTO_GRPC_SRCS} --plugin=protoc-gen-grpc=${grpc_ROOT}/bin/grpc_cpp_plugin &&
                cp ${VENDOR_CMAKE_DIR}/CMakeLists.txt.etcdapi <SOURCE_DIR>/CMakeLists.txt
        CMAKE_ARGS ${etcdapi_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        DEPENDS etcd_proto protobuf grpc
)

ExternalProject_Get_Property(etcdapi INSTALL_DIR)
else()
message(STATUS "etcdapi has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(etcdapi)
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of etcdapi: ${INSTALL_DIR}")

set(etcdapi_ROOT ${INSTALL_DIR})
set(etcdapi_INCLUDE_DIR ${etcdapi_ROOT}/include)
set(etcdapi_LIB_DIR ${etcdapi_ROOT}/lib)
set(etcdapi_LIB_A ${etcdapi_LIB_DIR}/libetcdapi_proto.a)

include_directories(${etcdapi_INCLUDE_DIR})
