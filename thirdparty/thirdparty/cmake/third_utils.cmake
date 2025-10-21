
if ("${THIRDPARTY_SRC_DIR}" STREQUAL "")
    message(STATUS "use default thirdparty config dir")
    set(THIRDPARTY_SRC_DIR ${PROJECT_SOURCE_DIR}/../vendor)
endif()

get_filename_component(ABSOLUTE_THIRDPARTY_SRC_DIR ${THIRDPARTY_SRC_DIR} ABSOLUTE)
set(THIRDPARTY_SRC_DIR ${ABSOLUTE_THIRDPARTY_SRC_DIR})
message(STATUS "THIRDPARTY_SRC_DIR is ${THIRDPARTY_SRC_DIR}")

if ("${THIRDPARTY_INSTALL_DIR}" STREQUAL "")
    message(STATUS "use default thirdparty install dir")
    set(THIRDPARTY_INSTALL_DIR ${THIRDPARTY_SRC_DIR}/out)
endif()

if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" OS_RELEASE_CONTENT REGEX "^ID=")
    string(REGEX REPLACE "^ID=\"?([^\"\n]+)\"?.*" "\\1" LINUX_DISTRIBUTION "${OS_RELEASE_CONTENT}")
else()
    set(LINUX_DISTRIBUTION "Unknown")
endif()

message("Linux Distribution: ${LINUX_DISTRIBUTION}")

include(ExternalProject)
message(STATUS "THIRDPARTY_INSTALL_DIR is ${THIRDPARTY_INSTALL_DIR}")
set(EP_BUILD_DIR "${THIRDPARTY_INSTALL_DIR}/${LINUX_DISTRIBUTION}")
get_filename_component(ABSOLUTE_EP_BUILD_DIR ${EP_BUILD_DIR} ABSOLUTE)
set(EP_BUILD_DIR ${ABSOLUTE_EP_BUILD_DIR})
message(STATUS "EP_BUILD_DIR: ${EP_BUILD_DIR}")
set_property(DIRECTORY PROPERTY EP_BASE ${EP_BUILD_DIR})

set(CODE_GENERATE_FLAGS "-fno-common -freg-struct-return -fstrong-eval-order")
set(OPTIMIZE_FLAGS "-ffunction-sections -fdata-sections")
set(COMPILE_SAFE_FLAGS "-fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(THIRDPARTY_COMMON_FLAGS "-O2 -fuse-ld=gold -pipe ${CODE_GENERATE_FLAGS} ${OPTIMIZE_FLAGS} ${COMPILE_SAFE_FLAGS} -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-std=gnu11 ${THIRDPARTY_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-std=c++14 ${THIRDPARTY_COMMON_FLAGS}")
set(THIRDPARTY_C_FLAGS "${CMAKE_C_FLAGS_RELEASE}")
set(THIRDPARTY_CXX_FLAGS "${CMAKE_CXX_FLAGS_RELEASE}")

set(LINK_SAFE_FLAGS "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -s")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINK_SAFE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie ${LINK_SAFE_FLAGS}")
set(THIRDPARTY_LINK_FLAGS "-Wl,--gc-sections -Wl,--build-id=none -Wl,-z,origin ${LINK_SAFE_FLAGS}")

function(INSTALL_TARGET target)
    install(TARGETS ${target}
            ARCHIVE DESTINATION ${INSTALL_LIBDIR}
            LIBRARY DESTINATION ${INSTALL_LIBDIR}
            RUNTIME DESTINATION ${INSTALL_BINDIR})
endfunction()

function(PATCH_FOR_SOURCE WORK_DIR PATCH_FILES)
    set(PATCH_FILE "${WORK_DIR}/patch_applied")
    if(NOT EXISTS ${PATCH_FILE})
    set(INDEX 1)
    while(INDEX LESS ${ARGC})
        execute_process(COMMAND patch -p1 -i ${ARGV${INDEX}}
                WORKING_DIRECTORY ${WORK_DIR}
                RESULT_VARIABLE _RET)
        if(NOT _RET EQUAL "0")
            message("patch ${ARGV${INDEX}} for ${WORK_DIR} failed, result: ${_RET}")
        endif()
        math(EXPR INDEX "${INDEX}+1")
    endwhile()
    file(WRITE ${PATCH_FILE} "Patch applied")
    endif()
endfunction()