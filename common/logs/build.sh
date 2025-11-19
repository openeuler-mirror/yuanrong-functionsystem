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
set -o nounset
set -o pipefail

# Go to repo location.
if ! cd "$(readlink -e "$(dirname "${0}")")"; then
    echo "Failed to cd source dir."
    exit 1
fi
source common.sh || exit 1

function checkopts()
{
    while getopts 'j:t:c:x:T:' opt
    do
        case "$opt" in
        j)
            if [ ${OPTARG} -gt $CPU_NUM ]; then
                echo "The -j $OPTARG is over the max logical cpu count($CPU_NUM), set BUILD_THREAD_NUM to ($CPU_NUM)"
            fi
            CPU_NUM="${OPTARG}"
            ;;
        t)
            if [ "${OPTARG^^}" = "ON" ]; then
                BUILD_TEST="ON"
            fi
            ;;
        x)
            if [ "${OPTARG^^}" = "ON" ]; then
                DOWNLOAD_OPENSRC="ON"
            fi
            ;;
        c)
            if [ "X${OPTARG}" = "Xoff" ]; then
                log_info "Coverage reports is disabled"
            elif [ "X${OPTARG}" = "Xon" ]; then
                BUILD_GCOV=ON
            elif [ "X${OPTARG}" = "Xhtml" ]; then
                BUILD_GCOV=ON
                GEN_LLT_REPORT=ON
            else
                log_error "Invalid value ${OPTARG} for option -c, choose from off/on/html"
                log_info "${USAGE}"
                exit 1
            fi
            ;;
        *)
            echo "command not recognized"
            exit 1
            ;;
        esac
    done
}

function configure()
{
    mkdir -p ${BUILD_DIR} && \
    cd ${BUILD_DIR} && \
    cmake "${PROJECT_DIR}" -DLOGS_BUILD_TEST=${BUILD_TEST} \
        -DCMAKE_INSTALL_PREFIX=${OUTPUT_DIR} \
        -DBUILD_CONFIG_DIR="${BUILD_CONFIG_DIR}" \
        -DBUILD_GCOV="${BUILD_GCOV}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        ..
}

function compile()
{
    cd ${BUILD_DIR} && make -j "${CPU_NUM}"
}

function test()
{
    echo "build test: ${BUILD_TEST}"
    if [ "$BUILD_TEST" = "ON" ]; then
        cd ${BUILD_DIR}/test && ctest --output-on-failure
        "$(pwd)"/${UT_EXECUTABLE}
    fi
}

function gen_llt_report()
{
   if [ "X${GEN_LLT_REPORT}" = "XON" ]; then
       echo "Generate test report"
       lcov --capture --directory "${PROJECT_DIR}" --output-file  coverage.info_tmp
       lcov --extract coverage.info_tmp '*src/*' --output-file coverage.info
       genhtml coverage.info --output-directory coverage_report --ignore-errors source
   fi
}

function install()
{
    cd ${BUILD_DIR} && make install
}

checkopts $@
configure
compile
test
gen_llt_report
install
