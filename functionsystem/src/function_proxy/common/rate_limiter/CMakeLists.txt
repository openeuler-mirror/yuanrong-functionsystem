aux_source_directory(${CMAKE_CURRENT_LIST_DIR} RATE_LIMITER_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${RATE_LIMITER_SRCS})