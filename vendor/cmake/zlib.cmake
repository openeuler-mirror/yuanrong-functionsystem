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

set(src_dir ${VENDOR_SRC_DIR}/zlib)
set(src_name zlib)

set(${src_name}_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
    -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
)

add_library(
    minizip STATIC
    ${src_dir}/contrib/minizip/crypt.h
    ${src_dir}/contrib/minizip/ioapi.h
    ${src_dir}/contrib/minizip/ioapi.c
    ${src_dir}/contrib/minizip/unzip.h
    ${src_dir}/contrib/minizip/unzip.c
)
set_target_properties(minizip PROPERTIES
                      ARCHIVE_OUTPUT_DIRECTORY ${HISTORY_INSTALLLED}/lib)
target_compile_options(minizip PRIVATE -Wno-unused-parameter -Wno-sign-conversion -Wno-conversion)

file(COPY ${src_dir}/contrib/minizip/crypt.h DESTINATION ${HISTORY_INSTALLLED}/include/minizip)
file(COPY ${src_dir}/contrib/minizip/ioapi.h DESTINATION ${HISTORY_INSTALLLED}/include/minizip)
file(COPY ${src_dir}/contrib/minizip/unzip.h DESTINATION ${HISTORY_INSTALLLED}/include/minizip)

ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(${src_name})
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(${src_name}_ROOT ${INSTALL_DIR})
set(${src_name}_INCLUDE_DIR ${${src_name}_ROOT}/include)
set(${src_name}_LIB_DIR ${${src_name}_ROOT}/lib)
set(${src_name}_LIB ${${src_name}_LIB_DIR}/libz.so)
set(${src_name}_MINIZIP_LIB ${${src_name}_LIB_DIR}/libminizip.a)

include_directories(${${src_name}_INCLUDE_DIR})

install(FILES ${${src_name}_LIB_DIR}/libz.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libz.so.1 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libz.so.1.3.1 DESTINATION lib)