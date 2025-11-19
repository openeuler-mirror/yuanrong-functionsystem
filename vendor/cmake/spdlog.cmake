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

set(src_dir ${VENDOR_SRC_DIR}/spdlog)
set(src_name spdlog)

set(${src_name}_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DSPDLOG_BUILD_SHARED:BOOL=ON
    -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
    -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
set(patch_files
        ${VENDOR_PATCHES_DIR}/spdlog/0001-PATCH-change-namespace-and-library-name-with-yr.patch
        ${VENDOR_PATCHES_DIR}/spdlog/ignore_rename_exception.patch)
PATCH_FOR_SOURCE(${src_dir} ${patch_files})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
)

ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(${src_name})
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(spdlog_ROOT ${INSTALL_DIR})
set(${src_name}_INCLUDE_DIR ${INSTALL_DIR}/include)
set(${src_name}_LIB_DIR ${INSTALL_DIR}/lib)
set(${src_name}_LIB ${${src_name}_LIB_DIR}/libspdlog.so)

include_directories(${${src_name}_INCLUDE_DIR})

install(FILES ${${src_name}_LIB_DIR}/libspdlog.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libspdlog.so.1.12 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libspdlog.so.1.12.0 DESTINATION lib)