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

set(src_dir ${ROOT_DIR}/datasystem)

get_filename_component(absolute_src_dir ${src_dir} ABSOLUTE)
set(src_dir ${absolute_src_dir})

set(datasystem_INCLUDE_DIR ${src_dir}/output/sdk/cpp/include)
set(datasystem_LIB_DIR ${src_dir}/output/sdk/cpp/lib)
set(datasystem_LIB ${datasystem_LIB_DIR}/libdatasystem.so)

include_directories(${datasystem_INCLUDE_DIR})

message(STATUS "datasystem lib dir: ${datasystem_LIB_DIR}")
file(GLOB DS_SPDLOG "${datasystem_LIB_DIR}/libds-spdlog.so*")
file(GLOB TBB "${datasystem_LIB_DIR}/libtbb*")
file(GLOB ZMQ "${datasystem_LIB_DIR}/libzmq*")
file(GLOB DS_CURL "${datasystem_LIB_DIR}/libcurl*")
install(FILES ${datasystem_LIB_DIR}/libdatasystem.so DESTINATION ${INSTALL_LIBDIR})
install(FILES ${DS_SPDLOG} DESTINATION ${INSTALL_LIBDIR})
install(FILES ${TBB} DESTINATION ${INSTALL_LIBDIR})
install(FILES ${ZMQ} DESTINATION ${INSTALL_LIBDIR})
install(FILES ${DS_CURL} DESTINATION ${INSTALL_LIBDIR})
install(FILES ${datasystem_LIB_DIR}/libacl_plugin.so DESTINATION ${INSTALL_LIBDIR} OPTIONAL)