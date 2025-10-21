cmake_minimum_required(VERSION 3.12.1)

if(${STATIC_LIB} STREQUAL "on")
  add_subdirectory(clt)
else()

    if(DEFINED DEPEND_PATH)
      set(PROTO_C ${DEPEND_PATH}/PROTOBUF_C/bin/protoc-c)
      execute_process(COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/schema
                      ${PROTO_C} -I${CMAKE_CURRENT_SOURCE_DIR}/schema --c_out=${CMAKE_CURRENT_SOURCE_DIR}/schema ${CMAKE_CURRENT_SOURCE_DIR}/schema/clientserver.proto
                      COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/schema
                      ${PROTO_C} -I${CMAKE_CURRENT_SOURCE_DIR}/schema --c_out=${CMAKE_CURRENT_SOURCE_DIR}/schema ${CMAKE_CURRENT_SOURCE_DIR}/schema/base.proto)
    endif()

    # LITEBUS TESTS.
    ################
    set(LITEBUS_TESTS_SRC
      main.cpp
        actor_tests.cpp
        asyncafter_test.cpp
        async_test.cpp
        collect_test.cpp
        defer_test.cpp
        exec_tests.cpp
        future_test.cpp
        http_parser_test.cpp
        option_tests.cpp
        result_test.cpp
        flagparser_tests.cpp
        try_test.cpp
        uuid_tests.cpp
        tcp/tcp_test.cpp
        timer/timer_tests.cpp
        utils_test
        sysmgr_test.cpp
        ${LIBPROCESS_TCP}
    )

    if(${HTTP_ENABLED} STREQUAL "on")
      set(LITEBUS_TESTS_SRC
        ${LITEBUS_TESTS_SRC}
        http_test/http_test.cpp
        #http_test/http_decoder_test.cpp
      )
    endif(${HTTP_ENABLED} STREQUAL "on")

    if(${UDP_ENABLED} STREQUAL "on")
      set(LITEBUS_TESTS_SRC
        ${LITEBUS_TESTS_SRC}
        udp/udp_test.cpp
        ${LIBPROCESS_UDP}
      )
    endif(${UDP_ENABLED} STREQUAL "on")

    set(LITEBUS_STABILITY_TESTS_SRC
        main_actor_stability.cpp
        actor_stability_test.cpp
    )

    if(${SSL_ENABLED} STREQUAL "on")
    set(LITEBUS_TESTS_SRC
        ${LITEBUS_TESTS_SRC}
        tcp/ssl_test.cpp
        http_test/https_test.cpp
        http_test/http_test_mutithread.cpp
        ${LIBPROCESS_SSL}
    )
      set(LITEBUS_TEST_LIB_DIRS
          litebus_shared
          ${gtest_LIB_A}
          ${gmock_LIB_A}
          ${crypto_LIB}
          ${ssl_LIB}
          ${securec_LIB}
          ${yrlogs_LIB}
          pthread
          rt
      )
    else()
      set(LITEBUS_TEST_LIB_DIRS
          litebus_shared
          ${gtest_LIB_A}
          ${gmock_LIB_A}
          ${securec_LIB}
          ${yrlogs_LIB}
          pthread
          rt
      )
    endif(${SSL_ENABLED} STREQUAL "on")

    if(${HTTP_ENABLED} STREQUAL "on")
        message(STATUS "HTTP build")

      set(LITEBUS_TEST_LIB_DIRS
        ${LITEBUS_TEST_LIB_DIRS}
        ${HTTP_PARSER_DFLAG}
        ${curl_LIB}
      )
    endif(${HTTP_ENABLED} STREQUAL "on")

    list(APPEND LITEBUS_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

    include_directories(${PROJECT_SOURCE_DIR}/test/)
    include_directories(${PROJECT_SOURCE_DIR}/include)
    include_directories(${PROJECT_SOURCE_DIR}/src)

    if(${HTTP_ENABLED} STREQUAL "on")
      include_directories(${HTTP_PARSER_INCLUDE_DIR})
    endif(${HTTP_ENABLED} STREQUAL "on")

    link_directories(${SECUREC_LIB_DIR})

    if(${HTTP_ENABLED} STREQUAL "on")
      link_directories(${HTTP_PARSER_LIB_DIR})
    endif(${HTTP_ENABLED} STREQUAL "on")

    add_executable(${LITEBUS_TESTS_TARGET} ${LITEBUS_TESTS_SRC})
    target_compile_options(${LITEBUS_TESTS_TARGET} PRIVATE -Wno-error)
    target_link_libraries(${LITEBUS_TESTS_TARGET} ${LITEBUS_TEST_LIB_DIRS})
    add_dependencies(${LITEBUS_TESTS_TARGET} curl)

    add_executable(testTcpServer tcp/test_server.cpp)
    target_compile_options(testTcpServer PRIVATE -Wno-error)
    target_link_libraries(testTcpServer ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(testTcpServer curl)
    if(${SSL_ENABLED} STREQUAL "on")
    add_executable(testSslServer tcp/test_ssl_server.cpp)
    target_compile_options(testSslServer PRIVATE -Wno-error)
    target_link_libraries(testSslServer ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(testSslServer curl)
    endif(${SSL_ENABLED} STREQUAL "on")

    if(${UDP_ENABLED} STREQUAL "on")
      add_executable(testUdpServer udp/udp_server.cpp)
      target_compile_options(testUdpServer PRIVATE -Wno-error)
      target_link_libraries(testUdpServer ${LITEBUS_TEST_LIB_DIRS} pthread)
      add_dependencies(testUdpServer curl)
    endif(${UDP_ENABLED} STREQUAL "on")

    add_executable(perfTestTcp tcp/perf_test.cpp)
    target_compile_options(perfTestTcp PRIVATE -Wno-error)
    target_link_libraries(perfTestTcp ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(perfTestTcp curl)
    #profiler
    add_executable(throughput_performance benchmark/throughput_performance.cpp)
    target_compile_options(throughput_performance PRIVATE -Wno-error)
    target_link_libraries(throughput_performance ${LITEBUS_TEST_LIB_DIRS} pthread ${yrlogs_LIB})
    add_dependencies(throughput_performance curl)

    ##TODO: open it in future
    add_executable(actor-test actor_test.cpp)
    target_compile_options(actor-test PRIVATE -Wno-error)
    target_link_libraries(actor-test ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(actor-test curl)

    add_executable(stability-test stability_test.cpp)
    target_compile_options(stability-test PRIVATE -Wno-error)
    target_link_libraries(stability-test ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(stability-test curl)

    add_executable(pingpong pingpong.cpp)
    target_compile_options(pingpong PRIVATE -Wno-error)
    target_link_libraries(pingpong ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(pingpong curl)

    add_executable(server-stability server_multiactor.cpp)
    target_compile_options(server-stability PRIVATE -Wno-error)
    target_link_libraries(server-stability ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(server-stability curl)

    add_executable(client-stability ${LITEBUS_STABILITY_TESTS_SRC})
    target_compile_options(client-stability PRIVATE -Wno-error)
    target_link_libraries(client-stability ${LITEBUS_TEST_LIB_DIRS} pthread)
    add_dependencies(client-stability curl)

    if(${HTTP_ENABLED} STREQUAL "on")
      add_executable(http-server-test http_test/httpserver.cpp)
      target_compile_options(http-server-test PRIVATE -Wno-error)
      add_executable(httpkmsg_enable_server http_test/httpkmsg_enable_server.cpp)
      target_compile_options(httpkmsg_enable_server PRIVATE -Wno-error)
      target_link_libraries(http-server-test ${LITEBUS_TEST_LIB_DIRS} pthread)
      target_link_libraries(httpkmsg_enable_server ${LITEBUS_TEST_LIB_DIRS} pthread)
      add_dependencies(http-server-test curl)
      add_dependencies(httpkmsg_enable_server curl)

        add_executable(http-client-test http_test/httpclient.cpp)
        target_compile_options(http-client-test PRIVATE -Wno-error)
        add_executable(httpkmsg_enable_client http_test/httpkmsg_enable_client.cpp)
        target_compile_options(httpkmsg_enable_client PRIVATE -Wno-error)
        target_link_libraries(http-client-test ${LITEBUS_TEST_LIB_DIRS} pthread)
        target_link_libraries(httpkmsg_enable_client ${LITEBUS_TEST_LIB_DIRS} pthread)
        add_dependencies(http-client-test curl)
        add_dependencies(httpkmsg_enable_client curl)

    endif(${HTTP_ENABLED} STREQUAL "on")

    add_dependencies(${LITEBUS_TESTS_TARGET} litebus_shared curl)

    add_test(NAME LitebusTests COMMAND ./${LITEBUS_TESTS_TARGET})

endif(${STATIC_LIB} STREQUAL "on")
