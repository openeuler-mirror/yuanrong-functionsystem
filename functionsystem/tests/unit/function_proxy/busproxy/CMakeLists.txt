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

add_subdirectory(invocation_handler)
add_subdirectory(registry)
add_subdirectory(startup)
add_subdirectory(instance_proxy)
add_subdirectory(instance_view)
add_subdirectory(memory_monitor)

get_property(SRC_BUSPROXY_INCLUDE_DIR GLOBAL PROPERTY "SRC_BUSPROXY_INCLUDE_DIR")

target_include_directories(${UNIT_TEST_MODULE} PRIVATE ${SRC_BUSPROXY_INCLUDE_DIR})
