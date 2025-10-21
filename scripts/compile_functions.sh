#!/bin/bash
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

set -e

RUNTIME_PATH="${BASE_DIR}/.."

function runtime_compile() {
    local build_version=$1
    local build_runtime=$2
    local cpp_enable="false"
    local java_enable="false"
    local python_enable="false"
    local go_enable="false"
    if [ "$build_runtime" == "all" ]; then
        cpp_enable="true"
        java_enable="true"
        python_enable="true"
        go_enable="true"
    else
        local runtime_array=(${build_runtime//,/ })
        for var in ${runtime_array[@]}
        do
           if [ "$var" == "cpp" ]; then
               cpp_enable="true"
           elif [ "$var" == "java"  ]; then
               java_enable="true"
           elif [ "$var" == "python" ]; then
               python_enable="true"
           elif [ "$var" == "go" ]; then
               go_enable="true"
           else
               log_error "unknown runtime type ${var}"
          fi
        done
    fi
    if "${cpp_enable}" == "true" && cd "${RUNTIME_PATH}/runtime-cpp/build" && ! bash build.sh -v "$build_version" -p "${PROJECT_DIR}/open_source" -d "${RUNTIME_PATH}"; then
        log_error "Failed to build cpp-runtime"
        return 1
    fi
    if  "${java_enable}" == "true" && cd "${RUNTIME_PATH}/runtime-java/build" && ! bash build.sh -v "$build_version" -t true; then
        log_error "Failed to build java-runtime"
        return 1
    fi
    if  "${python_enable}" == "true" && cd "${RUNTIME_PATH}/runtime-python/build" && ! bash build.sh -a; then
        log_error "Failed to build python-runtime"
        return 1
    fi
    if  "${go_enable}" == "true" && cd "${RUNTIME_PATH}/runtime-go/build" && ! bash build.sh; then
        log_error "Failed to build go-runtime"
        return 1
    fi
    return 0
}
