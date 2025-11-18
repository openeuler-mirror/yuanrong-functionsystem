# Build the libs grpc depends
# c-ares
set(c-ares_src_dir ${VENDOR_SRC_DIR}/c-ares)
message(STATUS "c-ares src dir: ${c-ares_src_dir}")
set(re2_src_dir ${VENDOR_SRC_DIR}/re2)
message(STATUS "re2 src dir: ${re2_src_dir}")
set(grpc_src_dir ${VENDOR_SRC_DIR}/grpc)
message(STATUS "grpc src dir: ${grpc_src_dir}")

set(c-ares_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCARES_SHARED:BOOL=OFF
    -DCARES_STATIC:BOOL=ON
    -DCARES_STATIC_PIC:BOOL=ON
    -DHAVE_LIBNSL:BOOL=OFF
    -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
    -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
    -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/c-ares")
set(patch_files ${VENDOR_PATCHES_DIR}/c-ares/backport-CVE-2024-25629.patch)
if (NOT EXISTS ${HISTORY_INSTALLLED})
PATCH_FOR_SOURCE(${c-ares_src_dir} ${patch_files})
EXTERNALPROJECT_ADD(c-ares
    SOURCE_DIR ${c-ares_src_dir}
    DOWNLOAD_COMMAND ""
    CMAKE_ARGS ${c-ares_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

ExternalProject_Get_Property(c-ares INSTALL_DIR)
else()
message(STATUS "c-ares has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(c-ares)
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of c-ares: ${INSTALL_DIR}")

set(c-ares_ROOT ${INSTALL_DIR})

set(re2_CMAKE_ARGS
    -Dre2_ABSL_PROVIDER:STRING=package
    -Dabsl_DIR:PATH=${absl_ROOT}/lib/cmake/absl
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=TRUE
    -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
    -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
    -DCMAKE_CXX_STANDARD=17
)

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/re2")
if (NOT EXISTS ${HISTORY_INSTALLLED})
EXTERNALPROJECT_ADD(re2
    SOURCE_DIR ${re2_src_dir}
    DOWNLOAD_COMMAND ""
    CMAKE_ARGS ${re2_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    DEPENDS absl
)

ExternalProject_Get_Property(re2 INSTALL_DIR)
else()
message(STATUS "re2 has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(re2)
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of re2: ${INSTALL_DIR}")

set(re2_ROOT ${INSTALL_DIR})
set(re2_INCLUDE_DIR ${re2_ROOT}/include)
include_directories(${re2_INCLUDE_DIR})

set(grpc_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DBUILD_SHARED_LIBS:BOOL=ON
    -DgRPC_INSTALL:BOOL=ON
    -DgRPC_BUILD_TESTS:BOOL=OFF
    -DgRPC_PROTOBUF_PROVIDER:STRING=package
    -DgRPC_PROTOBUF_PACKAGE_TYPE:STRING=CONFIG
    -DProtobuf_DIR:PATH=${protobuf_PKG_PATH}
    -Dutf8_range_DIR:PATH=${utf8_range_PKG_PATH}
    -DgRPC_ABSL_PROVIDER:STRING=package
    -Dabsl_DIR:PATH=${absl_ROOT}/lib/cmake/absl
    -DgRPC_CARES_PROVIDER:STRING=package
    -Dc-ares_DIR:PATH=${c-ares_ROOT}/lib/cmake/c-ares
    -DgRPC_RE2_PROVIDER:STRING=package
    -Dre2_DIR:PATH=${re2_ROOT}/lib/cmake/re2
    -DgRPC_SSL_PROVIDER:STRING=package
    -DgRPC_ZLIB_PROVIDER:STRING=package
    -DOPENSSL_ROOT_DIR:STRING=${openssl_ROOT}
    -DZLIB_ROOT:PATH=${zlib_ROOT}
    -DCMAKE_CXX_FLAGS_RELEASE=${THIRDPARTY_CXX_FLAGS}
    -DCMAKE_SHARED_LINKER_FLAGS=${THIRDPARTY_LINK_FLAGS}
    -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
    -DgRPC_DOWNLOAD_ARCHIVES=OFF
    -DCMAKE_CXX_STANDARD=17
)

message(STATUS "grpc build type: ${CMAKE_BUILD_TYPE}")

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/grpc")
if (NOT EXISTS ${HISTORY_INSTALLLED})
set(grpc_patch_files ${VENDOR_PATCHES_DIR}/grpc/grpc_1_65_4_gcc_7_3.patch)
PATCH_FOR_SOURCE(${grpc_src_dir} ${grpc_patch_files})

EXTERNALPROJECT_ADD(grpc
    SOURCE_DIR ${grpc_src_dir}
    DOWNLOAD_COMMAND ""
    CMAKE_ARGS ${grpc_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
    BUILD_COMMAND LD_LIBRARY_PATH=${protobuf_LIB_DIR}:$ENV{LD_LIBRARY_PATH} ${CMAKE_MAKE_PROGRAM} > /dev/null 2>&1
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    DEPENDS protobuf c-ares re2 absl zlib openssl
)

ExternalProject_Get_Property(grpc INSTALL_DIR)
else()
message(STATUS "grpc has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(grpc)
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of grpc: ${INSTALL_DIR}")

set(grpc_ROOT ${INSTALL_DIR})
set(grpc_INCLUDE_DIR ${grpc_ROOT}/include)
set(grpc_LIB_DIR ${grpc_ROOT}/lib)
set(grpcpp_LIB ${grpc_LIB_DIR}/libgrpc++.so  ${grpc_LIB_DIR}/libgrpc.so)
set(grpcpp_reflection_LIB ${grpc_LIB_DIR}/libgrpc++_reflection.so)
set(gpr_LIB ${grpc_LIB_DIR}/libgpr.so)

include_directories(${grpc_INCLUDE_DIR})

file(GLOB GRPC_LIBS
        "${grpc_LIB_DIR}/libgrpc.so*"
        "${grpc_LIB_DIR}/libgrpc++*.so*"
        "${grpc_LIB_DIR}/libgpr.so*"
        "${grpc_LIB_DIR}/libupb*"
        "${grpc_LIB_DIR}/libutf8*"
        "${grpc_LIB_DIR}/libaddress_sorting.so*")
install(FILES ${GRPC_LIBS} DESTINATION lib)

# Generate gRPC protobuf cc files.
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
#       gRPC protobuf source files to be compiled.
#
#   SOURCE_ROOT <dir>
#       gRPC protobuf source files root directory, default is ${CMAKE_CURRENT_SOURCE_DIR},
#       if protobuf source files are not in ${CMAKE_SOURCE_DIR}, this variable must be set.
#
#   PROTO_DEPEND <target>
#       If the generated cc files need to depend some target this variable must be set.
function(GENERATE_GRPC_CPP SRCS HDRS TARGET_DIR)
    set(options)
    set(one_value_args SOURCE_ROOT PROTO_DEPEND)
    set(multi_value_args PROTO_FILES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT ARG_PROTO_FILES)
        message(FATAL_ERROR "GENERATE_GRPC_CPP() called without any proto files")
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

    message(STATUS "GENERATE_GRPC_CPP protobuf lib: ${protobuf_LIB_DIR}")

    foreach (_PROTO_FILE ${ARG_PROTO_FILES})
        get_filename_component(_ABS_FILE   ${_PROTO_FILE} ABSOLUTE)
        get_filename_component(_ABS_DIR    ${_PROTO_FILE} DIRECTORY)
        get_filename_component(_PROTO_NAME ${_PROTO_FILE} NAME_WE)
        get_filename_component(_PROTO_DIR  ${_PROTO_FILE} PATH)
        file(RELATIVE_PATH _REL_DIR ${ARG_SOURCE_ROOT} ${_ABS_DIR})
        file(MAKE_DIRECTORY ${TARGET_DIR}/${_REL_DIR})
        list(APPEND ${SRCS} ${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.pb.cc)
        list(APPEND ${SRCS} ${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.grpc.pb.cc)
        list(APPEND ${HDRS} ${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.pb.h)
        list(APPEND ${HDRS} ${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.grpc.pb.h)
        add_custom_command(
            OUTPUT "${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.grpc.pb.cc" "${TARGET_DIR}/${_REL_DIR}/${_PROTO_NAME}.grpc.pb.h"
            COMMAND ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${grpc_LIB_DIR}:${protobuf_LIB_DIR}
                    ${protobuf_ROOT}/bin/protoc
            ARGS ${_PROTO_IMPORT_ARGS}
                 -I ${_PROTO_DIR}
                 --grpc_out ${TARGET_DIR}
                 --cpp_out ${TARGET_DIR}
                 --plugin=protoc-gen-grpc=${grpc_ROOT}/bin/grpc_cpp_plugin
                 ${_ABS_FILE}
            DEPENDS ${_ABS_FILE}
            COMMENT "Running c++ grpc protocol compiler on ${_PROTO_FILE}" VERBATIM)

        if (ARG_PROTO_DEPEND)
            add_custom_target(GRPC_LIB_DEPEND_${_PROTO_NAME} DEPENDS
                            "${TARGET_DIR}/${_PROTO_NAME}.grpc.pb.cc"
                            "${TARGET_DIR}/${_PROTO_NAME}.grpc.pb.h")
            add_dependencies(${ARG_PROTO_DEPEND} GRPC_LIB_DEPEND_${_PROTO_NAME})
        endif()
    endforeach ()

    set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
    set(${SRCS} ${${SRCS}} PARENT_SCOPE)
    set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()