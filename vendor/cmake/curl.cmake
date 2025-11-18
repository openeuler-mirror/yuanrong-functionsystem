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

set(src_name curl)
set(src_dir ${VENDOR_SRC_DIR}/curl)
set(curl_PATCHES
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2024-6197.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2024-6874.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2024-7264.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2024-8096.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2024-9681.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2024-11053.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2025-0167.patch
        ${VENDOR_PATCHES_DIR}/curl/backport-CVE-2025-0725.patch
)

set(${src_name}_CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DOPENSSL_INCLUDE_DIR=${openssl_INCLUDE_DIR}
        -DOPENSSL_LIBRARY_DIR=${openssl_LIB_DIR}
        -DOPENSSL_ROOT_DIR=${openssl_ROOT}
        -DOPENSSL_LIBRARIES=${openssl_LIB_DIR}
        -DCURL_DISABLE_TESTS=ON
        -DBUILD_CURL_EXE=OFF
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
PATCH_FOR_SOURCE(${src_dir} ${curl_PATCHES})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        DEPENDS openssl)

ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(${src_name})
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(${src_name}_ROOT ${INSTALL_DIR})
set(${src_name}_INCLUDE_DIR ${INSTALL_DIR}/include)
set(${src_name}_LIB_DIR ${INSTALL_DIR}/lib)
set(${src_name}_LIB ${${src_name}_LIB_DIR}/libcurl.so)

include_directories(${${src_name}_INCLUDE_DIR})
link_directories(${${src_name}_LIB_DIR})
file(GLOB curl_LIBS "${${src_name}_LIB_DIR}/libcurl.so*")
install(FILES ${curl_LIBS} DESTINATION lib)
