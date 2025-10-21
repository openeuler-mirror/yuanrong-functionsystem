if(${HTTP_ENABLED} STREQUAL "on")
  target_sources(litebus_obj PRIVATE
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_actor.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_client.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_connect.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_decoder.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_iomgr.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_parser.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_pipeline_proxy.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/http_sysmgr.cpp
  )
endif(${HTTP_ENABLED} STREQUAL "on")
