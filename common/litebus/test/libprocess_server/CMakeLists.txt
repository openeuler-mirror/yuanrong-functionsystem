set(CMAKE_VERBOSE_MAKEFILE true)
set(LIBPROCESS_SERVER_TESTS_SRC
    main.cpp
    libprocess_server_test.cpp
)

set(LIBPROCESS_DIR
   ${PROJECT_SOURCE_DIR}/build/3rdparty/LIBPROCESS
)

include_directories(${LIBPROCESS_DIR}/include)
include_directories(${LIBPROCESS_DIR}/include/process)
include_directories(${LIBPROCESS_DIR}/include/protobuf)
include_directories(${LIBPROCESS_DIR}/include/picojson)
include_directories(${LIBPROCESS_DIR}/include/http_parser)
include_directories(${LIBPROCESS_DIR}/include/glog)
link_directories(${LIBPROCESS_DIR}/lib)

set(LIBPROCESS_LIBS
    process
    event
    event_pthreads
    event_core
    event_openssl
    ${securec_LIB}
    ${yrlogs_LIB}
    pthread
)

add_executable(libprocess_server_test ${LIBPROCESS_SERVER_TESTS_SRC})
set_target_properties(libprocess_server_test PROPERTIES COMPILE_FLAGS "-Wno-unused-local-typedefs")
target_link_libraries(libprocess_server_test ${LIBPROCESS_LIBS})


add_executable(libprocess_tcp_server libprocess_tcp_server.cpp)
set_target_properties(libprocess_tcp_server PROPERTIES COMPILE_FLAGS "-Wno-unused-local-typedefs")
target_link_libraries(libprocess_tcp_server ${LIBPROCESS_LIBS})

add_executable(libprocess_ssl_server libprocess_ssl_server.cpp)
set_target_properties(libprocess_ssl_server PROPERTIES COMPILE_FLAGS "-Wno-unused-local-typedefs")
target_link_libraries(libprocess_ssl_server ${LIBPROCESS_LIBS})

add_executable(libprocess_udp_server libprocess_udp_server.cpp)
set_target_properties(libprocess_udp_server PROPERTIES COMPILE_FLAGS "-Wno-unused-local-typedefs")
target_link_libraries(libprocess_udp_server ${LIBPROCESS_LIBS})
