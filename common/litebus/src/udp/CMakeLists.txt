if(${UDP_ENABLED} STREQUAL "on")
  target_sources(litebus_obj PRIVATE
                 ${CMAKE_CURRENT_SOURCE_DIR}/udp_adapter.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/udpmgr.cpp
  )
endif(${UDP_ENABLED} STREQUAL "on")
