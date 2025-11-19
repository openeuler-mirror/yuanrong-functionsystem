cmake_minimum_required(VERSION 3.12.1)
project(litebus LANGUAGES C CXX)

option(CODE_COVERAGE "off")
option(STATIC_LIB "off")
include(CheckCXXCompilerFlag)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "compile tools: ccache")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# 引入三方件与配置
# 设置 vendor 文件夹路径 | VENDOR_DIR
set(VENDOR_DIR ${CMAKE_CURRENT_LIST_DIR}/../../vendor)
message(STATUS "VENDOR_DIR: ${VENDOR_DIR}")
# 设置三方件编译配置，导入公共参数
include(${VENDOR_DIR}/vendor_utils.cmake)
# 引入三方件编译的 cmake 文件
list(APPEND CMAKE_MODULE_PATH ${VENDOR_CMAKE_DIR})
include(openssl)
include(securec)
if(${BUILD_TESTCASE} STREQUAL "on")
  include(curl)
  include(gtest_1_12_1)
endif()
# 引入二方件编译的 cmake 文件
include(${CMAKE_CURRENT_LIST_DIR}/../logs/logs.cmake)
# ---------------------------------------------------------------------------------------

check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
find_package(Threads REQUIRED)

set(LITEBUS_PACKAGE_VERSION 0.0.1)
set(LITEBUS_PACKAGE_SOVERSION 0)
set(LITEBUS_TARGET litebus-${LITEBUS_PACKAGE_VERSION})


if(${CMAKE_BUILD_TYPE} STREQUAL "release")
  set(CMAKE_CXX_FLAGS "-std=c++17 -D__STDC_FORMAT_MACROS -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--build-id=none -Wl,--disable-new-dtags")
elseif(${CMAKE_BUILD_TYPE} STREQUAL  "debug")
  set(CMAKE_CXX_FLAGS "-std=c++17 -D__STDC_FORMAT_MACROS -D_GNU_SOURCE -DDEBUG_TIME -D_DEBUG -ggdb")
else()
  message(FATAL_ERROR "unrecognized option, ${CMAKE_BUILD_TYPE}")
endif(${CMAKE_BUILD_TYPE} STREQUAL  "release")

if(${CODE_COVERAGE} STREQUAL "on")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage --coverage")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-inline -fno-inline-small-functions -fno-default-inline")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage --coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-inline -fno-inline-small-functions -fno-default-inline")
endif(${CODE_COVERAGE} STREQUAL "on")

if("${ENABLE_ASAN}" STREQUAL "on")
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -g -fsanitize=address -fsanitize-recover=address -fsanitize=leak -fno-delete-null-pointer-checks")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wno-reorder -fsanitize=address -fsanitize-recover=address -fsanitize=leak -fno-delete-null-pointer-checks")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
endif("${ENABLE_ASAN}" STREQUAL "on")

if("${BIT_COMP_FLAGS}" STREQUAL "-m32")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
endif("${BIT_COMP_FLAGS}" STREQUAL "-m32")

if(${HTTP_ENABLED} STREQUAL "on")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHTTP_ENABLED")
endif(${HTTP_ENABLED} STREQUAL "on")

if(${UDP_ENABLED} STREQUAL "on")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUDP_ENABLED")
endif(${UDP_ENABLED} STREQUAL "on")

if(${SSL_ENABLED} STREQUAL "on")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSSL_ENABLED")
endif(${SSL_ENABLED} STREQUAL "on")

if(${LIBPROCESS_INTERWORK_ENABLED} STREQUAL "on")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLIBPROCESS_INTERWORK_ENABLED")
endif(${LIBPROCESS_INTERWORK_ENABLED} STREQUAL "on")

find_program(LINKER_LLD NAMES ld.lld lld)
if (LINKER_LLD)
    set(CMAKE_CXX_LD_FLAGS "-fuse-ld=lld")
    set(CMAKE_C_LD_FLAGS "-fuse-ld=lld")
else ()
    # If lld does not exist, use gold and enable parallel linking.
    set(CMAKE_CXX_LD_FLAGS "-fuse-ld=gold -Wl,--threads")
    set(CMAKE_C_LD_FLAGS "-fuse-ld=gold -Wl,--threads")
endif ()

set(WARN_FLAGS "-Wall -Werror -Wextra -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-float-equal -Wtrampolines -Wformat=2 -Wdate-time -Wcast-align -Wunused -Wno-deprecated-declarations")
set(CXX_WANR_FLAGS "${WARN_FLAGS} -Woverloaded-virtual -Wnon-virtual-dtor -Wdelete-non-virtual-dtor")
set(C_WANR_FLAGS "${WARN_FLAGS}")
set(LANGUAGE_FLAGS "-fsigned-char -funsigned-char")
set(CODE_GENERATE_FLAGS "-fno-common -freg-struct-return -fstrong-eval-order")
set(OPTIMIZE_FLAGS "-fno-omit-frame-pointer")
set(COMPILE_SAFE_FLAGS "-fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_LD_FLAGS} -pipe ${CXX_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -fPIC")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -fPIC -DNDEBUG")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_CXX_LD_FLAGS} -pipe ${C_WANR_FLAGS} ${LANGUAGE_FLAGS} ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -fPIC")
set(CMAKE_C_FLAGS_RELEASE "-O2 -fPIC -DNDEBUG")

set(LINK_SAFE_FLAGS "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -s")
set(CMAKE_SHARED_LINKER_FLAGS "${LINK_SAFE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "-pie ${LINK_SAFE_FLAGS}")

set(LITEBUS_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(LITEBUS_TESTS_TARGET litebus-test
  CACHE STRING "Target we use to refer to tests for the litebus library")

add_subdirectory(src)
if(${BUILD_TESTCASE} STREQUAL "on")
  add_subdirectory(test)
endif(${BUILD_TESTCASE} STREQUAL "on")

install(FILES ${CMAKE_SOURCE_DIR}/include/litebus.h DESTINATION include)
install(FILES ${CMAKE_SOURCE_DIR}/include/litebus.hpp DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/actor DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/async DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/timer DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/exec DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/utils DESTINATION include)

if(${HTTP_ENABLED} STREQUAL "on")
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/httpd DESTINATION include)
endif(${HTTP_ENABLED} STREQUAL "on")

if(${SSL_ENABLED} STREQUAL "on")
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ssl DESTINATION include)
endif(${SSL_ENABLED} STREQUAL "on")