aux_source_directory(${CMAKE_CURRENT_LIST_DIR} COMMON_DRIVER_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${COMMON_DRIVER_SRCS})