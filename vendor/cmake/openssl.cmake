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

set(src_name openssl)
set(src_dir ${VENDOR_SRC_DIR}/openssl)
set(${src_name}_COMMON_FLAGS "-O2 -fuse-ld=gold -pipe -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DNDEBUG")
set(${src_name}_C_FLAGS "-std=gnu11 ${${src_name}_COMMON_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS}")
set(${src_name}_CXX_FLAGS "-std=c++14 ${${src_name}_COMMON_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS}")

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
set(patch_files
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-0727.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-0727-2.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-2511.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-4741.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-4741-2.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-5535.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-9143.patch
    ${VENDOR_PATCHES_DIR}/openssl/backport-CVE-2024-13176.patch
)

if (NOT EXISTS ${HISTORY_INSTALLLED})
PATCH_FOR_SOURCE(${src_dir} ${patch_files})

EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND cd ${src_dir} && ./config --prefix=<INSTALL_DIR> CXXFLAGS=${${src_name}_CXX_FLAGS} CFLAGS=${${src_name}_C_FLAGS} LDFLAGS=${LINK_SAFE_FLAGS} shared enable-ssl3 enable-ssl3-method > /dev/null 2>&1
        LOG_BUILD ON
        LOG_INSTALL ON
        BUILD_IN_SOURCE 1)

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
set(crypto_LIB ${${src_name}_LIB_DIR}/libcrypto.so)
set(ssl_LIB ${${src_name}_LIB_DIR}/libssl.so)
set(crypto_LIB_A ${${src_name}_LIB_DIR}/libcrypto.a)
set(ssl_LIB_A ${${src_name}_LIB_DIR}/libssl.a)

include_directories(${${src_name}_INCLUDE_DIR})

install(FILES ${${src_name}_LIB_DIR}/libssl.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libssl.so.1.1 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libcrypto.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libcrypto.so.1.1 DESTINATION lib)