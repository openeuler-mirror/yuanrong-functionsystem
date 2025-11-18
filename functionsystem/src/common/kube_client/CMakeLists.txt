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

project(kube_client)
add_library(kube_client STATIC "")
set_target_properties(kube_client PROPERTIES UNITY_BUILD ON)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/api KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/health_monitor KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/common KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/container KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/deployment KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/node KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/pod KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/volume KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/lease KUBE_CLIENT_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/model/horizontal_pod_autoscaler KUBE_CLIENT_SRCS)
add_dependencies(kube_client status)
target_sources(kube_client PRIVATE ${KUBE_CLIENT_SRCS})
target_link_libraries(kube_client PRIVATE status ${litebus_ALL_LIB} metrics_adapter)