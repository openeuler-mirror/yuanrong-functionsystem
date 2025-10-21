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

CUR_DIR=$(dirname $(readlink -f "$0"))
BUILD_TMP_DIR="${CUR_DIR}/build"
VENDOR_DIR="${CUR_DIR}/../../../vendor"
BUILD_TEST_THIRDPARTY="ON"
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"

[ ! -d "${VENDOR_DIR}" ] && echo "vendor directory does not exist" && exit 1

while getopts 'j:r' opt; do
    case "$opt" in
    j)
        if [ ${OPTARG} -gt $(($CPU_NUM * 2)) ]; then
            echo "The -j $OPTARG is over the max logical cpu count($CPU_NUM) * 2"
        fi
        JOB_NUM="${OPTARG}"
        ;;
    r)
        BUILD_TEST_THIRDPARTY="OFF"
        ;;
    *)
        ;;
    esac
done

mkdir -p "${BUILD_TMP_DIR}"
pushd "${BUILD_TMP_DIR}"
cmake .. -DBUILD_TEST_THIRDPARTY=${BUILD_TEST_THIRDPARTY}
cmake --build ${BUILD_TMP_DIR} --parallel ${JOB_NUM}
popd