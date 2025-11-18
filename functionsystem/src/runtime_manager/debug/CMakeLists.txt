# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DEBUG_SERVER_SRCS)

target_sources(runtime_manager_lib PRIVATE ${DEBUG_SERVER_SRCS})