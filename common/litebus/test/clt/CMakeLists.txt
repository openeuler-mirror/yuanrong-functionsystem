cmake_minimum_required(VERSION 3.12.1)


if(STATIC_LIB)
 set(LITEBUS_TEST_LIB_DIRS_A
      ${LITEBUS_TARGET}_static
      ${securec_LIB}
      ${yrlogs_LIB}
      rt
      ${decrypt_static}
      ${LITEBUS_OPENSSL_DLIBS}
  )
endif(STATIC_LIB)


list(APPEND LITEBUS_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(${PROJECT_SOURCE_DIR}/test/clt)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROTOBUF_C_INCLUDE_DIR})
include_directories(${CJSON_INCLUDE_DIR})


link_directories(${PROTOBUF_C_LIB_DIR})
link_directories(${CJSON_LIB_DIR})

if(STATIC_LIB)
  link_directories(${HTTP_PARSER_LIB_DIR})
endif(STATIC_LIB)


if(${HTTP_ENABLED} STREQUAL "on")
if(STATIC_LIB)
  message(STATUS "LITEBUS_TEST_LIB_DIRS_A: ${LITEBUS_TEST_LIB_DIRS_A} ;")
  add_executable(litebus_clt litebus_client.cpp)
  target_link_libraries(litebus_clt ${LITEBUS_TEST_LIB_DIRS_A} -ldl pthread)
endif(STATIC_LIB)

endif(${HTTP_ENABLED} STREQUAL "on")

