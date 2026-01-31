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

set(src_dir ${VENDOR_SRC_DIR}/opentelemetry)
set(src_name opentelemetry)

set(${src_name}_CMAKE_ARGS
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_BUILD_TYPE=Release
        -DWITH_OTLP_GRPC=ON # build OTLP GRPC exporter
        -DBUILD_TESTING=OFF # do not build ut code in opentelemetry
        -DWITH_EXAMPLES=OFF # do not build examples code in opentelemetry
        # No dependency is transferred. need to declare the address of the compilation product on which the GRPC depends, such as absl and c-ares.
        -DWITH_ABSEIL:BOOL=ON # Indicates that the local environment already has compiled artifacts of absl when protobuf version is 3.22 or upper
        -DgRPC_DIR=${grpc_ROOT}/lib/cmake/grpc
        -Dabsl_DIR:PATH=${absl_ROOT}/lib/cmake/absl
        -Dc-ares_DIR:PATH=${c-ares_ROOT}/lib/cmake/c-ares
        -Dre2_DIR:PATH=${re2_ROOT}/lib/cmake/re2
        -DOPENSSL_ROOT_DIR:STRING=${openssl_ROOT}
        -DZLIB_ROOT:PATH=${zlib_ROOT}
        -DProtobuf_DIR:PATH=${protobuf_PKG_PATH}
        -Dutf8_range_DIR:PATH=${utf8_range_PKG_PATH}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DOPENTELEMETRY_INSTALL=ON
        -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
        -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
        -DCMAKE_CXX_STANDARD=17 # absl use cpp17 to compile
)


set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
    # Compile opentelemetry depends on opentelemetry-proto, need to copy the source code to the opentelemetry/third_party directory.
    file(COPY ${VENDOR_SRC_DIR}/opentelemetry_proto DESTINATION ${VENDOR_SRC_DIR}/opentelemetry/third_party)
    EXTERNALPROJECT_ADD(${src_name}
            SOURCE_DIR ${src_dir}
            CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=lib
            BUILD_COMMAND bash -c "export LD_LIBRARY_PATH=${protobuf_LIB_DIR}:${grpc_LIB_DIR}:$ENV{LD_LIBRARY_PATH} \
                                && ${CMAKE_MAKE_PROGRAM} -j ${BUILD_THREAD_NUM}"
            LOG_CONFIGURE ON
            LOG_BUILD ON
            LOG_INSTALL ON
            DEPENDS protobuf grpc
    )
    ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
    message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLLED}")
    add_custom_target(${src_name})
    set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(${src_name}_INCLUDE_DIR ${INSTALL_DIR}/include)
set(${src_name}_LIB_DIR ${INSTALL_DIR}/lib)

include_directories(${${src_name}_INCLUDE_DIR})
include_directories(${${src_name}_INCLUDE_DIR}/opentelemetry/exporters)

message("opentelemetry.cmake finish")

install(DIRECTORY ${${src_name}_LIB_DIR}/ DESTINATION lib FILES_MATCHING PATTERN "*.so*")