
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} OBSERVER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/data_plane_observer OBSERVER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/control_plane_observer OBSERVER_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${OBSERVER_SRCS})

target_include_directories(function_proxy_common_lib PRIVATE ${CMAKE_CURRENT_LIST_DIR})