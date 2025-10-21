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

BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)
OUTPUT_DIR="${BASE_DIR}/../output"
PREBUILD_BIN_PATH_YUANRONG=${OUTPUT_DIR}/openyuanrong
PREBUILD_BIN_PATH_TOOLS=${OUTPUT_DIR}/tools
. ${BASE_DIR}/utils.sh

CURRENT_ARCH=$(uname -m)

# whether show help info
SHOW_HELP=""

# default tag
TAG="v0.0.1"

BUILD_TARGETS="whl"
WHL_PYTHON_ONLY="false"
PYTHON_BIN_PATH="python3"

function build_whl () {
    bash ${BASE_DIR}/pypi/package.sh \
        --prebuild_yuanrong=${PREBUILD_BIN_PATH_YUANRONG} \
        --tag ${TAG} \
        --arch=${CURRENT_ARCH} \
        --python_only=${WHL_PYTHON_ONLY} \
        --python_bin_path=${PYTHON_BIN_PATH} \
        --output=${OUTPUT_DIR}
}

function parse_args () {
    getopt_cmd=$(getopt -o o:t:h -l output_dir:,tag:,prebuild_yuanrong:,targets:,whl_python_only:,python_bin_path:,help -- "$@")
    [ $? -ne 0 ] && exit 1
    eval set -- "$getopt_cmd"
    while true; do
        case "$1" in
        -o|--output_dir) OUTPUT_DIR=$2 && shift 2 ;;
        -t|--tag) TAG=$2 && shift 2 ;;
        --prebuild_yuanrong) PREBUILD_BIN_PATH_YUANRONG=$2 && shift 2 ;;
        --arch) CURRENT_ARCH=$2 && shift 2 ;;
        --targets) BUILD_TARGETS=$2 && shift 2 ;;
        --whl_python_only) WHL_PYTHON_ONLY=$2 && shift 2 ;;
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
    -o|--output_dir              the output dir (=${OUTPUT_DIR})
    -t|--tag                     the version and release (=${TAG})
    --prebuild_yuanrong          the prebuild yuanrong.tar.gz path (=${PREBUILD_BIN_PATH_YUANRONG})
    --targets                    build target packages (=${BUILD_TARGETS})
    --arch                       the arch (=${CURRENT_ARCH})
    --python_bin_path            the python path (=${PYTHON_BIN_PATH})
    -h|--help                    show this help info
EOF
        exit 1
    fi
}

function main () {
    parse_args "$@"
    IFS=',' read -r -a array <<< "$BUILD_TARGETS"
    for target in "${array[@]}"; do
        case "$target" in
            whl)
                log_info "building $target"
                build_whl
                ;;
            *)
                log_error "Unknown build target: $target"
                ;;
        esac
    done
}

main $@
