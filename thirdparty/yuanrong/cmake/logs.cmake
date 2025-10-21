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

set(src_name yrlogs)
set(src_dir ${ROOT_DIR}/common/logs)
get_filename_component(ABSOLUTE_src_dir ${src_dir} ABSOLUTE)
set(src_dir ${ABSOLUTE_src_dir})

message(STATUS "logs src dir: ${src_dir}")

if (NOT EXISTS ${src_dir}/output)
    if (EXISTS ${src_dir})
        message(STATUS "begin build yrlogs, download opensrc: ${DOWNLOAD_OPENSRC}, thirdparty src dir: ${THIRDPARTY_SRC_DIR}")
        execute_process(COMMAND bash build.sh -x ${DOWNLOAD_OPENSRC} -T ${THIRDPARTY_SRC_DIR} WORKING_DIRECTORY ${src_dir})
    endif()
endif()

set(${src_name}_INCLUDE_DIR ${src_dir}/output/include)
set(${src_name}_LIB_DIR ${src_dir}/output/lib)
set(${src_name}_LIB ${${src_name}_LIB_DIR}/libyrlogs.so ${${src_name}_LIB_DIR}/libspdlog.so)

include_directories(${${src_name}_INCLUDE_DIR})

message(STATUS "yrlogs include dir: ${${src_name}_INCLUDE_DIR}")

install(FILES ${${src_name}_LIB_DIR}/libyrlogs.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libspdlog.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libspdlog.so.1.12 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libspdlog.so.1.12.0 DESTINATION lib)