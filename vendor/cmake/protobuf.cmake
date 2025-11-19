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

set(abseil-cpp_src_dir ${VENDOR_SRC_DIR}/abseil-cpp)
message(STATUS "abseil-cpp src dir: ${abseil-cpp_src_dir}")
set(src_dir ${VENDOR_SRC_DIR}/protobuf)
set(src_name protobuf)

set(absl_CMAKE_ARGS
        -DCMAKE_BUILD_TYPE:STRING=Release
        -DBUILD_SHARED_LIBS:BOOL=FALSE
        -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=TRUE
        -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
        -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
        -DCMAKE_CXX_STANDARD=17
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/absl")
if (NOT EXISTS ${HISTORY_INSTALLLED})
    EXTERNALPROJECT_ADD(absl
            SOURCE_DIR ${abseil-cpp_src_dir}
            DOWNLOAD_COMMAND ""
            CMAKE_ARGS ${absl_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
            LOG_CONFIGURE ON
            LOG_BUILD ON
            LOG_INSTALL ON
    )

    ExternalProject_Get_Property(absl INSTALL_DIR)
else()
    message(STATUS "absl has already installed in ${HISTORY_INSTALLLED}")
    add_custom_target(absl)
    set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()
message("install dir of absl: ${INSTALL_DIR}")

set(absl_ROOT ${INSTALL_DIR})
set(absl_INCLUDE_DIR ${absl_ROOT}/include)
set(absl_LIB_DIR ${absl_ROOT}/lib)
include_directories(${absl_INCLUDE_DIR})
file(GLOB absl_LIB
        "${absl_LIB_DIR}/*.a")

message(STATUS "protobuf build type: ${CMAKE_BUILD_TYPE}")

set(${src_name}_CMAKE_ARGS
    -Dprotobuf_BUILD_TESTS:BOOL=OFF
    -Dprotobuf_BUILD_SHARED_LIBS:BOOL=OFF
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_SKIP_RPATH:BOOL=TRUE
    -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
    -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
    -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
    -Dabsl_DIR:PATH=${absl_ROOT}/lib/cmake/absl
    -Dprotobuf_ABSL_PROVIDER=package
    -DCMAKE_CXX_STANDARD=17
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
set(patch_files ${VENDOR_PATCHES_DIR}/protobuf/protobuf_gcc_7_3.patch)
PATCH_FOR_SOURCE(${src_dir} ${patch_files})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=lib
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        DEPENDS absl
    )

ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(${src_name})
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(${src_name}_ROOT ${INSTALL_DIR})
set(${src_name}_INCLUDE_DIR ${${src_name}_ROOT}/include)
if (EXISTS ${${src_name}_ROOT}/lib64)
    set(${src_name}_LIB_DIR ${${src_name}_ROOT}/lib64)
    set(${src_name}_PKG_PATH ${${src_name}_ROOT}/lib64/cmake/protobuf)
    set(utf8_range_PKG_PATH ${${src_name}_ROOT}/lib64/cmake/utf8_range)
else()
    set(${src_name}_LIB_DIR ${${src_name}_ROOT}/lib)
    set(${src_name}_PKG_PATH ${${src_name}_ROOT}/lib/cmake/protobuf)
    set(utf8_range_PKG_PATH ${${src_name}_ROOT}/lib/cmake/utf8_range)
endif()

set(${src_name}_LIB_A ${${src_name}_LIB_DIR}/libprotobuf.a)
set(${src_name}_LIB
        "${${src_name}_LIB_DIR}/libprotobuf.a"
        "${${src_name}_LIB_DIR}/libutf8_validity.a"
        "${absl_LIB}"
        -lrt)
link_directories(${${src_name}_LIB_DIR})
include_directories(${${src_name}_INCLUDE_DIR})

# Generate protobuf cc files.
#
# SRCS is the output variable of the protobuf source files.
#
# HDRS is the output variable of the protobuf header files.
#
# TARGET_DIR is the generate cc files target directory.
#
# Additional optional arguments:
#
#   PROTO_FILES <file1> <file2> ...
#       Protobuf source files to be compiled.
#
#   SOURCE_ROOT <dir>
#       Protobuf source files root directory, default is ${CMAKE_CURRENT_SOURCE_DIR},
#       if protobuf source files are not in ${CMAKE_SOURCE_DIR}, this variable must
#       be set.
#
#   PROTO_DEPEND <target>
#       If the generated cc files need to depend some target this variable must be set.
function(GENERATE_PROTO_CPP SRCS HDRS TARGET_DIR)
    set(options)
    set(one_value_args SOURCE_ROOT PROTO_DEPEND)
    set(multi_value_args PROTO_FILES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT ARG_PROTO_FILES)
        message(SEND_ERROR "GENERATE_PROTO_CPP() called without any proto files")
    endif ()

    if (NOT ARG_SOURCE_ROOT)
        set(ARG_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    set(${SRCS})
    set(${HDRS})
    set(_PROTO_IMPORT_ARGS)

    # Add protobuf import dir to avoid import report by protoc compiler.
    foreach (_PROTO_FILE ${ARG_PROTO_FILES})
        get_filename_component(_ABS_FILE ${_PROTO_FILE} ABSOLUTE)
        get_filename_component(_ABS_PATH ${_ABS_FILE} PATH)
        list(FIND _PROTO_IMPORT_ARGS ${_ABS_PATH} _IMPORT_EXIST)
        if (${_IMPORT_EXIST} EQUAL -1)
            list(APPEND _PROTO_IMPORT_ARGS -I ${_ABS_PATH})
        endif()
    endforeach()

    foreach (_PROTO_FILE ${ARG_PROTO_FILES})
        get_filename_component(_ABS_FILE   ${_PROTO_FILE} ABSOLUTE)
        get_filename_component(_ABS_DIR    ${_PROTO_FILE} DIRECTORY)
        get_filename_component(_PROTO_NAME ${_PROTO_FILE} NAME_WE)
        get_filename_component(_PROTO_DIR  ${_PROTO_FILE} PATH)
        file(RELATIVE_PATH _REL_DIR ${ARG_SOURCE_ROOT} ${_ABS_DIR})
        file(MAKE_DIRECTORY ${TARGET_DIR}/${_REL_DIR})
        list(APPEND ${SRCS} ${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.pb.cc)
        list(APPEND ${HDRS} ${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.pb.h)
        add_custom_command(
            OUTPUT "${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.pb.cc" "${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.pb.h"
            COMMAND LD_LIBRARY_PATH=${protobuf_LIB_DIR} ${protobuf_ROOT}/bin/protoc
            ARGS ${_PROTO_IMPORT_ARGS} --cpp_out=${TARGET_DIR} ${_ABS_FILE}
            DEPENDS ${_ABS_FILE}
            COMMENT "Running c++ protocol buffer compiler on ${_PROTO_FILE}" VERBATIM)

        if (ARG_PROTO_DEPEND)
            add_custom_target(PROTO_LIB_DEPEND_${_PROTO_NAME} DEPENDS
                            "${TARGET_DIR}/${_PROTO_NAME}.pb.cc"
                            "${TARGET_DIR}/${_PROTO_NAME}.pb.h")
            add_dependencies(${ARG_PROTO_DEPEND} PROTO_LIB_DEPEND_${_PROTO_NAME})
        endif()
    endforeach ()

    set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
    set(${SRCS} ${${SRCS}} PARENT_SCOPE)
    set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()