aux_source_directory(${CMAKE_CURRENT_LIST_DIR} STATE_HANDLER_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${STATE_HANDLER_SRCS})