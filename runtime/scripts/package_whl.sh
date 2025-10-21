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
TAG=v0.0.1
BASE_DIR=$(
  cd "$(dirname "$0")"
  pwd
)
OUTPUT_DIR="${BASE_DIR}/../output"
. ${BASE_DIR}/package/utils.sh
function parse_args () {
    getopt_cmd=$(getopt -o t:h -l tag:,python_bin_path:,help -- "$@")
    [ $? -ne 0 ] && exit 1
    eval set -- "$getopt_cmd"
    while true; do
        case "$1" in
        -t|--tag) TAG=$2 && shift 2 ;;
        --python_bin_path) PYTHON_BIN_PATH=$2 && shift 2 ;;
        -h|--help) SHOW_HELP="true" && shift ;;
        --) shift && break ;;
        *) die "Invalid option: $1" && exit 1 ;;
        esac
    done
    if [ "$SHOW_HELP" != "" ]; then
        cat <<EOF
Usage:
  packaging rpm packages, args and default values:
    -t|--tag             the version and release (=${TAG})
    -h|--help            show this help info
EOF
        exit 1
    fi
}

function main () {
    parse_args "$@"
}
main $@

cd ${OUTPUT_DIR}
PYTHON_BIN_PATH=${PYTHON_BIN_PATH:-"python3"}

bash ${BASE_DIR}/package/package.sh package \
  --targets=whl \
  --prebuild_yuanrong=${OUTPUT_DIR}/openyuanrong \
  --tag=${TAG} \
  --output_dir=${OUTPUT_DIR}/trimmed \
  --whl_python_only=true \
  --python_bin_path=${PYTHON_BIN_PATH}

cp -ar ${OUTPUT_DIR}/trimmed/*.whl ${OUTPUT_DIR}

bash ${BASE_DIR}/package/package.sh package \
    --targets=whl \
    --prebuild_yuanrong=${OUTPUT_DIR}/openyuanrong \
    --tag=${TAG} \
    --output_dir=${OUTPUT_DIR}/full \
    --python_bin_path=${PYTHON_BIN_PATH}


