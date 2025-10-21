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

CUR_DIR=$(dirname $(readlink -f "$0"))
set -e

YUANRONG_DIR="${CUR_DIR}/../"
THIRD_PARTY_DIR="${CUR_DIR}/../vendor"
MODULES="all"

while getopts 'T:Y:M:' opt; do
    case "$opt" in
    Y)
        YUANRONG_DIR=$(readlink -f "${OPTARG}")
        ;;
    T)
        THIRD_PARTY_DIR=$(readlink -f "${OPTARG}")
        ;;
    M)
        MODULES="${OPTARG}"
        ;;
    *)
        log_error "Invalid command"
        exit 1
        ;;
    esac
done

bash ${CUR_DIR}/thirdparty/download_opensource.sh -T ${THIRD_PARTY_DIR} -M ${MODULES}