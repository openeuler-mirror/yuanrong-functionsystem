cmake_minimum_required(VERSION 3.16.1)
project(codegenerate)

list(APPEND CMAKE_MODULE_PATH ../functionsystem/cmake)

include(util)
include(external_lib/openssl)
include(external_lib/protobuf)
include(external_lib/zlib)
include(external_lib/grpc)

add_subdirectory(../functionsystem/src/common/proto ${CMAKE_CURRENT_BINARY_DIR}/proto)