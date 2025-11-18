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

set(src_name securec)
set(src_dir ${VENDOR_SRC_DIR}/libboundscheck)

message(STATUS "securec build type: ${CMAKE_BUILD_TYPE}")

set(HISTORY_INSTALLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLED})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND cp ${VENDOR_CMAKE_DIR}/CMakeLists.txt.securec <SOURCE_DIR>/CMakeLists.txt
        CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
)

ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLED}")
add_custom_target(${src_name})
set(INSTALL_DIR "${HISTORY_INSTALLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(securec_ROOT ${INSTALL_DIR})
set(securec_INCLUDE_DIR ${INSTALL_DIR}/include)
set(securec_LIB_DIR ${INSTALL_DIR}/lib)
set(securec_LIB ${securec_LIB_DIR}/libsecurec.so)

include_directories(${securec_INCLUDE_DIR})

install(FILES ${securec_LIB_DIR}/libsecurec.so DESTINATION lib)