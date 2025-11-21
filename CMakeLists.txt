# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16.1)
project(kernel LANGUAGES CXX C)

message(STATUS "kernel PROJECT_SOURCE_DIR：${PROJECT_SOURCE_DIR}")

# Check Git submodule status (only active in Git repositories)
execute_process(
    COMMAND git rev-parse --is-inside-work-tree
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE IS_GIT_REPO
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(IS_GIT_REPO)
    # Check submodule status recursively
    execute_process(
        COMMAND git submodule status --recursive
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE SUBMODULE_STATUS
        RESULT_VARIABLE GIT_SUBMOD_RESULT
        ERROR_QUIET
    )

    if(GIT_SUBMOD_RESULT EQUAL 0)
        # Detect uninitialized submodules (lines starting with '-')
        string(REGEX MATCH "(\n|^)-" UNINITIALIZED "${SUBMODULE_STATUS}")
        if(UNINITIALIZED)
            message(STATUS "Found uninitialized submodules. Initializing...")

            # Automatically initialize and update submodules
            execute_process(
                COMMAND git submodule update --init --recursive
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                RESULT_VARIABLE GIT_SUBMOD_UPDATE_RESULT
            )

            if(NOT GIT_SUBMOD_UPDATE_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to initialize submodules. Error code: ${GIT_SUBMOD_UPDATE_RESULT}")
            endif()

            message(STATUS "Submodules initialized successfully")
        endif()
    endif()
endif()

add_subdirectory(functionsystem)
add_subdirectory(runtime)