

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} POSIX_SERVICE_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${POSIX_SERVICE_SRCS})