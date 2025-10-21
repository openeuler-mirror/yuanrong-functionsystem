aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/proxy_view PROXY_VIEW_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/local_scheduler_view LOCAL_SCHEDULER_VIEW_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${INSTANCE_VIEW_SRCS} ${PROXY_VIEW_SRCS} ${LOCAL_SCHEDULER_VIEW_SRCS})