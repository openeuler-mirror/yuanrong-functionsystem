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

set(src_dir ${VENDOR_SRC_DIR}/jemalloc)
set(src_name jemalloc)

set(${src_name}_CXX_FLAGS "-DCMAKE_BUILD_TYPE:STRING=Release -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS} -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}")
set(JEMALLOC_PROF_FLAGS "")
set(JEMALLOC_LG_PAGE "")

if ("${JEMALLOC_PROF_ENABLE}" STREQUAL "ON")
    set(JEMALLOC_PROF_FLAGS "--enable-prof --enable-prof-libunwind")
endif ()

if (DEFINED ENV{FS_JEMALLOC_LG_PAGE})
    message(STATUS "jemalloc custom page size=2^$ENV{FS_JEMALLOC_LG_PAGE}")
    set(JEMALLOC_LG_PAGE "--with-lg-page=$ENV{FS_JEMALLOC_LG_PAGE}")
endif()

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND env LDFLAGS=${LINK_SAFE_FLAGS} CFLAGS=${THIRDPARTY_C_FLAGS} CXXFLAGS=${${src_name}_CXX_FLAGS} ./autogen.sh ${JEMALLOC_LG_PAGE} ${JEMALLOC_PROF_FLAGS} --prefix=<INSTALL_DIR>
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        BUILD_IN_SOURCE 1
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
set(${src_name}_LIB ${${src_name}_LIB_DIR}/libjemalloc.so)

include_directories(${${src_name}_INCLUDE_DIR})

install(FILES ${${src_name}_LIB_DIR}/libjemalloc.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libjemalloc.so.2 DESTINATION lib)