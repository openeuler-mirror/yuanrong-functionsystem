# Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SYSTEM_FUNCTION_LOADER_SRCS)

set(SYSTEM_FUNCTION_LOADER_LIB system_function_loader_lib) # for this directory
set(SYSTEM_FUNCTION_LOADER_LIB ${SYSTEM_FUNCTION_LOADER_LIB} PARENT_SCOPE) # for function_master directory

set_property(GLOBAL PROPERTY SYSTEM_FUNCTION_LOADER_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_property(GLOBAL PROPERTY SYSTEM_FUNCTION_LOADER_LIB ${SYSTEM_FUNCTION_LOADER_LIB})

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(${SYSTEM_FUNCTION_LOADER_LIB} STATIC
        ${SYSTEM_FUNCTION_LOADER_SRCS})
set_target_properties(${SYSTEM_FUNCTION_LOADER_LIB} PROPERTIES UNITY_BUILD ON)
target_compile_options(${SYSTEM_FUNCTION_LOADER_LIB} PRIVATE -fPIE)

target_include_directories(${SYSTEM_FUNCTION_LOADER_LIB} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})
add_dependencies(${SYSTEM_FUNCTION_LOADER_LIB} hex leader posix_pb utils meta_store_client file_monitor)
target_link_libraries(${SYSTEM_FUNCTION_LOADER_LIB} PUBLIC hex leader posix_pb utils meta_store_client file_monitor)