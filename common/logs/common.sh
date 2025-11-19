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

#--------------Constant--------------
export BASE_DIR="$(dirname $(readlink -f $0))"
export PROJECT_DIR="${BASE_DIR}"
export BUILD_DIR="${BASE_DIR}/build"
export OUTPUT_DIR="${BASE_DIR}/output"
export YR_ROOT_DIR="$(readlink -f "${BASE_DIR}/../..")"
export BUILD_CONFIG_DIR="${YR_ROOT_DIR}/build"
export CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"
UT_EXECUTABLE="logs_test"

#--------------Variable--------------
BUILD_TEST="OFF"
DOWNLOAD_OPENSRC="OFF"
BUILD_GCOV=OFF
GEN_LLT_REPORT=OFF

#--------------Function--------------
function check_number() {
    number_check='^([0-9]+)$'
    if [[ "$1" =~ ${number_check} ]]; then
        return 0
    else
        echo "Invalid value $1 for option -$2"
        exit 1
    fi
}

function strip_symbols() {
    local src_dir="$1"
    local dest_dir="$2"
    if [[ ! -d "${dest_dir}" ]]; then
        mkdir -p "${dest_dir}"
    fi

    for file in ${src_dir}/*; do
        local type
        type="$(file -b --mime-type ${file} | sed 's|/.*||')"
        if [[ ! -L "${file}" ]] && [[ ! -d "${file}" ]] && [[ "x${type}" != "xtext" ]]; then
            strip_file_symbols ${file} ${dest_dir}
        fi
    done
}

function strip_file_symbols() {
    local file="$1"
    local dest_dir="$2"
    echo "---- start to strip ${file}"
    local basename
    basename=$(basename "${file}")
    objcopy --only-keep-debug "${file}" "${dest_dir}/${basename}.sym"
    objcopy --add-gnu-debuglink="${dest_dir}/${basename}.sym" "${file}"
    objcopy --strip-all "${file}"
}