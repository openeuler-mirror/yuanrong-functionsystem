aux_source_directory(${CMAKE_CURRENT_LIST_DIR} IAM_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${IAM_SRCS})