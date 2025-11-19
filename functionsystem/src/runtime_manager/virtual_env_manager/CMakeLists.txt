# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} VIRTUAL_ENV_MANAGER_SRCS)

target_sources(runtime_manager_lib PRIVATE ${VIRTUAL_ENV_MANAGER_SRCS})