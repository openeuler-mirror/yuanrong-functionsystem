# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

enable_testing()

find_package(GTest REQUIRED)

add_executable(exec_session_test
    exec_session_test.cpp
)

target_include_directories(exec_session_test
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../..
)

target_link_libraries(exec_session_test
    exec_session
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME exec_session_test COMMAND exec_session_test)
