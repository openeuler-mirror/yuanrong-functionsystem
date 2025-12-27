aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DATA_OBJ_CLIENT_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${DATA_OBJ_CLIENT_SRCS})