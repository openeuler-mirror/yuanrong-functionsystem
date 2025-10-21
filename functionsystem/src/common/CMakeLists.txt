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
project(common)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/utils/version.h.in
    ${CMAKE_CURRENT_LIST_DIR}/utils/version.h
    @ONLY
)

add_library(explorer STATIC "")
set_target_properties(explorer PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/explorer EXPLORER_SRCS)
add_dependencies(explorer status meta_store_client)
target_sources(explorer PRIVATE ${EXPLORER_SRCS})
target_link_libraries(explorer PUBLIC meta_store_client status)

add_library(leader STATIC "")
set_target_properties(leader PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/leader LEADER_SRCS)
add_dependencies(leader status meta_store_client explorer)
target_sources(leader PRIVATE ${LEADER_SRCS})
target_link_libraries(leader PUBLIC status meta_store_client explorer)

add_library(register STATIC "")
set_target_properties(register PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/register REGISTER_SRCS)
add_dependencies(register status heartbeat)
target_sources(register PRIVATE ${REGISTER_SRCS})
target_link_libraries(register PUBLIC status heartbeat)

add_library(file_monitor STATIC "")
set_target_properties(file_monitor PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/file_monitor FILE_MONITOR_SRCS)
add_dependencies(file_monitor status)
target_sources(file_monitor PRIVATE ${FILE_MONITOR_SRCS})
target_link_libraries(file_monitor PUBLIC status posix_pb)

add_library(utils STATIC "")
set_target_properties(utils PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/utils FS_UTILS_SRCS)
add_dependencies(utils status)
target_sources(utils PRIVATE ${FS_UTILS_SRCS})
target_link_libraries(utils PUBLIC status ${crypto_LIB} ${ssl_LIB})


add_library(profile STATIC "")
set_target_properties(profile PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/profile PROFILE_SRCS)
add_dependencies(profile posix_pb)
target_sources(profile PRIVATE ${PROFILE_SRCS})
target_link_libraries(profile PUBLIC posix_pb)

add_library(resource_view STATIC "")
set_target_properties(resource_view PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/resource_view RESOURCE_VIEW_SRCS)
add_dependencies(resource_view posix_pb)
target_sources(resource_view PRIVATE ${RESOURCE_VIEW_SRCS})
target_link_libraries(resource_view PUBLIC posix_pb)

add_library(scheduler STATIC "")
set_target_properties(scheduler PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision/scheduler SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision/scheduler/priority_policy SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision/schedule_recorder SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision/queue SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision/performer SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_decision/preemption_controller SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler_framework SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler_framework/framework SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler_framework/utils SCHEDULER_SRCS)
add_dependencies(scheduler status posix_pb)
target_sources(scheduler PRIVATE ${SCHEDULER_SRCS})
target_link_libraries(scheduler PUBLIC status posix_pb)

add_library(schedule_plugin_common STATIC "")
set_target_properties(schedule_plugin_common PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/schedule_plugin/common PLUGIN_COMMON_SRCS)
add_dependencies(schedule_plugin_common status posix_pb resource_view)
target_sources(schedule_plugin_common PRIVATE ${PLUGIN_COMMON_SRCS})
target_link_libraries(schedule_plugin_common PUBLIC status posix_pb resource_view)

add_library(scheduler_topology STATIC "")
set_target_properties(scheduler_topology PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler_topology TOPOLOGY_SRCS)
add_dependencies(scheduler_topology posix_pb)
target_sources(scheduler_topology PRIVATE ${TOPOLOGY_SRCS})
target_link_libraries(scheduler_topology PUBLIC posix_pb)

add_library(service_json STATIC "")
set_target_properties(service_json PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/service_json SERVICE_JSON_SRCS)
add_dependencies(service_json posix_pb resource_view)
target_sources(service_json PRIVATE ${SERVICE_JSON_SRCS})
target_link_libraries(service_json PUBLIC posix_pb resource_view scheduler_topology)

add_library(network STATIC "")
set_target_properties(network PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/network NETWORK_SRCS)
target_sources(network PRIVATE ${NETWORK_SRCS})

add_library(meta_store_adapter STATIC "")
set_target_properties(meta_store_adapter PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/meta_store_adapter META_STORE_ADAPTER_SRCS)
add_dependencies(meta_store_adapter posix_pb resource_view)
target_sources(meta_store_adapter PRIVATE ${META_STORE_ADAPTER_SRCS})
target_link_libraries(meta_store_adapter PUBLIC meta_store_client)

add_subdirectory(yaml_tool)
add_subdirectory(schedule_plugin/prefilter/default_prefilter)
add_subdirectory(schedule_plugin/filter/default_filter)
add_subdirectory(schedule_plugin/filter/resource_selector_filter)
add_subdirectory(schedule_plugin/scorer/default_scorer)
add_subdirectory(schedule_plugin/filter/default_heterogeneous_filter)
add_subdirectory(schedule_plugin/scorer/default_heterogeneous_scorer)
add_subdirectory(schedule_plugin/filter/label_affinity_filter)
add_subdirectory(schedule_plugin/scorer/label_affinity_scorer)

set(PLUGIN
        -Wl,--whole-archive
        default_prefilter default_filter resource_selector_filter default_scorer default_heterogeneous_filter
        default_heterogeneous_scorer label_affinity_filter label_affinity_scorer
        -Wl,--no-whole-archive)

set_property(GLOBAL PROPERTY PLUGIN_LIB "${PLUGIN}")