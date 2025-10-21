if("${BUILD_CONFIG_DIR}" STREQUAL "")
  message(STATUS "use default thirdparty config dir")
  set(BUILD_CONFIG_DIR ${PROJECT_SOURCE_DIR}/../../thirdparty)
endif()

message(STATUS "BUILD_CONFIG_DIR is ${BUILD_CONFIG_DIR}")

list(APPEND CMAKE_MODULE_PATH ${BUILD_CONFIG_DIR}/thirdparty/cmake)
list(APPEND CMAKE_MODULE_PATH ${BUILD_CONFIG_DIR}/yuanrong/cmake)

include(logs)
include(third_utils)
include(openssl)
include(securec)

if(${BUILD_TESTCASE} STREQUAL "on")
  include(curl)
  include(gtest_1_12_1)
endif()
