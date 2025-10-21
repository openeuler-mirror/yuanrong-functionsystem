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
set -eo pipefail

GIT_BRANCH=PD_develop
BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"
JOB_NUM="$(($(grep -c 'processor' /proc/cpuinfo) + 1))"
YR_FUNCTION_SYSTEM_VERSION="0.0.1"
OUTPUT_DIR=$(realpath "$BASE_DIR"/../output)
echo "OUTPUT_DIR: ${OUTPUT_DIR}"

USAGE="
Usage: bash build.sh [-h] [-v] [-c] [-j <job_num>]\n\n

Options:\n
    -h show usage\n
    -v version\n
    -c clean vendor downloaded cache\n\n
    -j set the number of jobs run in parallel for compiling, for example: the number of processor(${CPU_NUM}). See build docs for details.\n\n

Example:\n
    bash build.sh -v 0.0.1 -j 16
"
