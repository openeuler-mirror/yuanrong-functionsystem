# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Licensed under the Apache License, Version 2.0 (the "License");

project(snapshot_storage)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SNAPSHOT_STORAGE_SRCS)
add_library(snapshot_storage STATIC ${SNAPSHOT_STORAGE_SRCS})
target_include_directories(snapshot_storage PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(snapshot_storage PUBLIC status utils kv_client ${datasystem_LIB} ${eSDKOBS_LIB})
