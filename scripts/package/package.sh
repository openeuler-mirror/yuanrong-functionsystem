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

# CURRENT_SH_DIR is /path/to/kernel/scripts/package/
CURRENT_SH_DIR=$(dirname $(realpath $0))

# KERNEL_ROOT_DIR is where the kernel at, path like /path/to/kernel
KERNEL_ROOT_DIR=$(realpath $CURRENT_SH_DIR/../../)

# to export make_combined_yuanrong_package
#           log_info/warn/error/die
. ${KERNEL_ROOT_DIR}/scripts/package/utils.sh

# Below parts are configureable
# will be set as below order, can use --help to check the positions

# output dir, default to the output with the same level as kernel
OUTPUT_DIR=$(realpath ${KERNEL_ROOT_DIR}/../output)

# prebuild yuanrong path, default under OUTPUT_DIR
PREBUILD_BIN_PATH_YUANRONG=${OUTPUT_DIR}/yuanrong
PREBUILD_BIN_PATH_TOOLS=${OUTPUT_DIR}/tools

IMAGE_REGISTRY="auto"
THIRDPARTY_IMAGE_REGISTRY="auto"

CURRENT_ARCH=$(uname -m)

# whether show help info
SHOW_HELP=""

# default tag
TAG="1.0.0.dev"

BUILD_TARGETS="whl,rpm"
WHL_PYTHON_ONLY="false"
PYTHON_BIN_PATH="python3"

function build_whl () {
    bash $KERNEL_ROOT_DIR/scripts/package/pypi/package.sh \
        --prebuild_yuanrong=${PREBUILD_BIN_PATH_YUANRONG} \
        --prebuild_tools=${PREBUILD_BIN_PATH_TOOLS} \
        --tag ${TAG} \
        --arch=${CURRENT_ARCH} \
        --python_only=${WHL_PYTHON_ONLY} \
        --python_bin_path=${PYTHON_BIN_PATH} \
        --output=${OUTPUT_DIR}
}

function build_rpm () {
    bash $KERNEL_ROOT_DIR/scripts/package/rpm/package.sh \
        --prebuild_yuanrong=${PREBUILD_BIN_PATH_YUANRONG} \
        --prebuild_tools=${PREBUILD_BIN_PATH_TOOLS} \
        --tag ${TAG} \
        --output=${OUTPUT_DIR}
}

function parse_args () {
    getopt_cmd=$(getopt -o o:t:h -l output_dir:,tag:,prebuild_yuanrong:,prebuild_tools:,targets:,image_registry:,thirdparty_image_registry:,whl_python_only:,python_bin_path:,help -- "$@")
    [ $? -ne 0 ] && exit 1
    eval set -- "$getopt_cmd"
    while true; do
        case "$1" in
        -o|--output_dir) OUTPUT_DIR=$2 && shift 2 ;;
        -t|--tag) TAG=$2 && shift 2 ;;
        --prebuild_yuanrong) PREBUILD_BIN_PATH_YUANRONG=$2 && shift 2 ;;
        --prebuild_tools) PREBUILD_BIN_PATH_TOOLS=$2 && shift 2 ;;
        --arch) CURRENT_ARCH=$2 && shift 2 ;;
        --targets) BUILD_TARGETS=$2 && shift 2 ;;
        --image_registry) IMAGE_REGISTRY=$2 && shift 2 ;;
        --thirdparty_image_registry) THIRDPARTY_IMAGE_REGISTRY=$2 && shift 2 ;;
        --whl_python_only) WHL_PYTHON_ONLY=$2 && shift 2 ;;
        --python_bin_path) PYTHON_BIN_PATH=$2 && shift 2 ;;
        -h|--help) SHOW_HELP="true" && shift ;;
        --) shift && break ;;
        *) die "Invalid option: $1" && exit 1 ;;
        esac
    done

    SEMVER=$(make_valid_semver ${TAG})

    if [ "$SHOW_HELP" != "" ]; then
        cat <<EOF
Usage:
  packaging rpm packages, args and default values:
    -o|--output_dir              the output dir (=${OUTPUT_DIR})
    -t|--tag                     the version and release (=${TAG}, version=${SEMVER})
    --prebuild_yuanrong          the prebuild yuanrong.tar.gz path (=${PREBUILD_BIN_PATH_YUANRONG})
    --prebuild_tools             the prebuild tools.tar path (=${PREBUILD_BIN_PATH_TOOLS})
    --targets                    build target packages (=${BUILD_TARGETS})
    --image_registry             the image registry of your images to put (=${IMAGE_REGISTRY})
    --thirdparty_image_registry  thirdparty image registry (for etcd/minio) (=${THIRDPARTY_IMAGE_REGISTRY})
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
            rpm)
                log_info "building $target"
                build_rpm
                ;;
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
