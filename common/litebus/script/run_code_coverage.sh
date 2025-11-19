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

dependent_branch=develop_rtos

CUR_DIR=$(dirname $(readlink -f $0))
TOP_DIR=${CUR_DIR}/..
YR_ROOT_DIR="$(readlink -f "${TOP_DIR}/../..")"
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"
JOB_NUM="$(($(grep -c 'processor' /proc/cpuinfo) + 1))"

#--------------Function--------------

checkopts()
{
    while getopts 'x:T:m:' opt
    do
        case "$opt" in
        m)
            #
            TEST_MODEL="$OPTARG"
            ;;
        *)
            echo "command not recognized"
            exit 1
            ;;
        esac
    done
}

checkopts $@

CUR_DIR=$(dirname $(readlink -f $0))
LITEBUS_ROOT_PATH=${CUR_DIR}/../
LITEBUS_SRC_BUILD_DIR=${LITEBUS_ROOT_PATH}/build/build/

#build testcases
sh ${LITEBUS_ROOT_PATH}/build/build.sh -D -c on || exit 1

#run tests
sh ${LITEBUS_ROOT_PATH}/test/run_tests.sh ${TEST_MODEL} || exit 1

cd ${LITEBUS_ROOT_PATH}
#remove old data
if [ -f "litebus_gcov.info" ];then
  rm litebus_gcov.info
fi

if [ -f "litebus_gcov_crc.info" ];then
  rm litebus_gcov_crc.info
fi

LITEBUS_GCOV_HTML=${LITEBUS_ROOT_PATH}/litebus_gcov_html
if [ ! -d "${LITEBUS_GCOV_HTML}" ]; then
    mkdir -p ${LITEBUS_GCOV_HTML}
else
    rm -rf ${LITEBUS_GCOV_HTML}/*
fi

find ${LITEBUS_SRC_BUILD_DIR} -name "*.gcda" -printf '%h\n' | sort | uniq | xargs -P ${JOB_NUM} -I {} sh -c 'lcov -c -d "{}" -o coverage_$(echo "{}" | sed "s/\//_/g").part_tmp'
lcov_files=$(find ${LITEBUS_ROOT_PATH} -name "coverage_*.part_tmp" -size +0)
lcov_files_with_a=$(echo "$lcov_files" | xargs -n 1 echo -a)
lcov $lcov_files_with_a -o ${coverage_file}_tmp

lcov -r ${coverage_file}_tmp '*/vendor/*' '*/logs/*' '*/spdlog/*' '*/build/*' '*/usr/*' '*/test/*' -o litebus_gcov_crc.info
genhtml litebus_gcov_crc.info -o ${LITEBUS_GCOV_HTML}

if [ ! -d "${LITEBUS_ROOT_PATH}/Litebus_Coverage" ];then
    mkdir -p ${LITEBUS_ROOT_PATH}/Litebus_Coverage
else
    rm -rf ${LITEBUS_ROOT_PATH}/Litebus_Coverage/*
fi
cp -r ${LITEBUS_ROOT_PATH}/litebus_gcov_html/* ${LITEBUS_ROOT_PATH}/Litebus_Coverage/

