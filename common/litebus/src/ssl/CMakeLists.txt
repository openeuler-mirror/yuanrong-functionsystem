if (${SSL_ENABLED} STREQUAL "on")
    target_sources(litebus_obj PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/openssl_wrapper.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/sensitive_value.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ssl_socket.cpp
    )
endif (${SSL_ENABLED} STREQUAL "on")