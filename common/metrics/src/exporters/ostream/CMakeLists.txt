# Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} EXPORTER_SRCS)

set(TARGET_NAME observability-metrics-exporter-ostream)

add_library(${TARGET_NAME} SHARED ${EXPORTER_SRCS})

target_include_directories(
        ${TARGET_NAME} PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")

target_link_libraries(${TARGET_NAME} PUBLIC observability-metrics-headers)

install(
        TARGETS ${TARGET_NAME}
        LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

