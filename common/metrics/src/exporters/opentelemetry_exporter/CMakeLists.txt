# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} OPENTELEMETRY_EXPORTER_SRCS)

set(TARGET_NAME observability-metrics-opentelemetry-exporter)

add_library(${TARGET_NAME} SHARED ${OPENTELEMETRY_EXPORTER_SRCS})

target_include_directories(
  ${TARGET_NAME} PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")
file(GLOB OTEL_LIBS ${opentelemetry_LIB_DIR}/*.so)
target_link_libraries(
  ${TARGET_NAME} PUBLIC observability-metrics-headers
  ${OTEL_LIBS}
  metrics-common
  ${litebus_ALL_LIB})

install(TARGETS ${TARGET_NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exporters/opentelemetry_exporter/include
  FILES_MATCHING
  PATTERN "*.h")
