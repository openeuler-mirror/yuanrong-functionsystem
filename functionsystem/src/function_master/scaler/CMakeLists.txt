# Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SCALER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/utils SCALER_UTILS_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/pool SCALER_POOL_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/long_run_pod_collector SCALER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/nuwa SCALER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/kuberay SCALER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/system_function_pod_manager SCALER_SRCS)

set(SCALER_LIB scaler_lib) # for this directory
set(SCALER_LIB ${SCALER_LIB} PARENT_SCOPE) # for function_master directory

set_property(GLOBAL PROPERTY SCALER_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_property(GLOBAL PROPERTY SCALER_LIB ${SCALER_LIB})

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_library(${SCALER_LIB} STATIC
        ${SCALER_UTILS_SRCS}
        ${SCALER_POOL_SRCS}
        ${SCALER_SRCS})
set_target_properties(${SCALER_LIB} PROPERTIES UNITY_BUILD ON)
target_compile_options(${SCALER_LIB} PRIVATE -fPIE)

target_include_directories(${SCALER_LIB} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${SCALER_LIB} PUBLIC master_common_lib kube_client meta_store_client)
