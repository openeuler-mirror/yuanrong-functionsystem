# Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.

set(src_dir ${VENDOR_SRC_DIR}/huaweicloud-sdk-c-obs)
set(src_name obs)

set(${src_name}_CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -Dcurl_ROOT=${curl_ROOT}
        -Dopenssl_ROOT=${openssl_ROOT}
        -Dspdlog_ROOT=${spdlog_ROOT}
        -Dsecurec_ROOT=${securec_ROOT}
        -DCMAKE_C_FLAGS_RELEASE=${THIRDPARTY_C_FLAGS}
)
message("spdlog_ROOT is ------------------ ${spdlog_ROOT}")

set(HISTORY_INSTALLLED "${EP_BUILD_DIR}/Install/${src_name}")
if (NOT EXISTS ${HISTORY_INSTALLLED})
EXTERNALPROJECT_ADD(${src_name}
        SOURCE_DIR ${src_dir}
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS ${${src_name}_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        PATCH_COMMAND patch -Np1 < ${VENDOR_PATCHES_DIR}/obs/obs-change-spdlog-with-yr.patch || echo 1
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        DEPENDS securec curl openssl spdlog
)

ExternalProject_Get_Property(${src_name} INSTALL_DIR)
else()
message(STATUS "${src_name} has already installed in ${HISTORY_INSTALLLED}")
add_custom_target(${src_name})
set(INSTALL_DIR "${HISTORY_INSTALLLED}")
endif()

message("install dir of ${src_name}: ${INSTALL_DIR}")

set(${src_name}_INCLUDE_DIR ${INSTALL_DIR}/include)
set(${src_name}_LIB_DIR ${INSTALL_DIR}/lib)
set(eSDKOBS_LIB ${${src_name}_LIB_DIR}/libeSDKOBS.so)
set(xml2_LIB ${${src_name}_LIB_DIR}/libxml2.so.2)

include_directories(${${src_name}_INCLUDE_DIR})

install(FILES ${${src_name}_LIB_DIR}/libeSDKLogAPI.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libeSDKOBS.so DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libiconv.so.2 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libxml2.so.2 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libcjson.so.1 DESTINATION lib)
install(FILES ${${src_name}_LIB_DIR}/libpcre.so DESTINATION lib)