#!/bin/bash
set -e
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
# Requirements.
# yum -y install rpmdevtools

# CURRENT_SH_DIR is /path/to/yuanrong/build/package/helm/
CURRENT_SH_DIR=$(dirname $(realpath $0))

# BUILD_ROOT_DIR is where the /path/to/yuanrong/
BUILD_ROOT_DIR=$(realpath $CURRENT_SH_DIR/../../)

# PROJECT_ROOT_DIR is where the /path/to/yuanrong/
PROJECT_ROOT_DIR=$(realpath $CURRENT_SH_DIR/../../../)

# Below parts are configureable
# will be set as below order, can use --help to check the positions

# output dir
OUTPUT_DIR=${PROJECT_ROOT_DIR}/output

# rpm build root dir, default under WORKING_DIR
RPM_BUILD_DIR=${BUILD_ROOT_DIR}/.build/rpmbuild

# yuanrong binary path, default under OUTPUT_DIR
PREBUILD_BIN_PATH_YUANRONG=${OUTPUT_DIR}/yuanrong.tar.gz
PREBUILD_BIN_PATH_TOOLS=${OUTPUT_DIR}/tools.tar.gz

# whether show help info
SHOW_HELP=""

# default tag
TAG="1.0.0.dev"
VERSION=$(echo $TAG | awk -F\. '{print $1 "." $2 "." $3 "." $4}')
RELEASE=$(echo $TAG | awk -F\. '{print $5}')

# to export make_combined_yuanrong_package
#           log_info/warn/error/die
. ${BUILD_ROOT_DIR}/package/utils.sh

function setup_workspace () {
    log_info "setting up rpm workspace"
    # clear it
    log_info "clear old workspace at $RPM_BUILD_DIR"
    [[ -n "${RPM_BUILD_DIR}" ]] && rm -rf "${RPM_BUILD_DIR}"

    # re-setup
    log_info "setup workspace at $RPM_BUILD_DIR"
    mkdir -p $RPM_BUILD_DIR/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

    log_info "remake combined yuanrong.tar.gz"
    make_combined_yuanrong_package \
        $PREBUILD_BIN_PATH_YUANRONG \
        $PREBUILD_BIN_PATH_TOOLS \
        $RPM_BUILD_DIR/yuanrong \
        /tmp/rpm_temp

    pushd $RPM_BUILD_DIR
    tar -zcf yuanrong.tar.gz yuanrong
    popd

    log_info "copy yuanrong.tar.gz binary to $RPM_BUILD_DIR/SOURCES"
    cp -rf $RPM_BUILD_DIR/yuanrong.tar.gz $RPM_BUILD_DIR/SOURCES

    log_info "rendering yuanrong spec file from template..."
    cp -f ${CURRENT_SH_DIR}/src/yuanrong.spec.template ${RPM_BUILD_DIR}/SPECS/yuanrong.spec
    sed -i "s/{{PH-VERSION}}/${VERSION}/g" ${RPM_BUILD_DIR}/SPECS/yuanrong.spec
    sed -i "s/{{PH-RELEASE}}/${RELEASE}/g" ${RPM_BUILD_DIR}/SPECS/yuanrong.spec
}

function build_rpm () {
    log_info "building rpm package..."

    # build
    rpmbuild --define "_topdir ${RPM_BUILD_DIR}" -bb ${RPM_BUILD_DIR}/SPECS/yuanrong.spec
    find ${RPM_BUILD_DIR}/RPMS/ -type f | xargs -I {} cp {} ${OUTPUT_DIR}/
}


function parse_args () {
    getopt_cmd=$(getopt -o o:t:h -l output_dir:,tag:,rpm_build_dir:,prebuild_yuanrong:,prebuild_tools:,help -- "$@")
    [ $? -ne 0 ] && exit 1
    eval set -- "$getopt_cmd"
    while true; do
        case "$1" in
        -o|--output_dir) OUTPUT_DIR=$2 && shift 2 ;;
        -t|--tag) TAG=$2 && shift 2 ;;
        --rpm_build_dir) RPM_BUILD_DIR=$2 && shift 2 ;;
        --prebuild_yuanrong) PREBUILD_BIN_PATH_YUANRONG=$2 && shift 2 ;;
        --prebuild_tools) PREBUILD_BIN_PATH_TOOLS=$2 && shift 2 ;;
        -h|--help) SHOW_HELP="true" && shift ;;
        --) shift && break ;;
        *) echo "Invalid option: $1" && exit 1 ;;
        esac
    done

    VERSION=$(echo $TAG | awk -F\. '{print $1 "." $2 "." $3}')
    RELEASE=$(echo $TAG | awk -F\. '{print $5}')

    if [ "$RELEASE" == "" ]; then
        # default release is always 0
        RELEASE="1"
    fi

    if [ "$SHOW_HELP" != "" ]; then
        cat <<EOF
Usage:
  packaging rpm packages, args and default values:
    -o|--output_dir      the output dir (=${OUTPUT_DIR})
    --rpm_build_dir      rpm build tree dir (=${RPM_BUILD_DIR})
    -t|--tag             the version and release (=${TAG}, version=${VERSION}, release=${RELEASE})
    --prebuild_yuanrong  the prebuild yuanrong.tar.gz path (=${PREBUILD_BIN_PATH_YUANRONG})
    --prebuild_tools     the prebuild tools.tar path (=${PREBUILD_BIN_PATH_TOOLS})
    -h|--help            show this help info
EOF
        exit 1
    fi
}

function main () {
    parse_args "$@"
    setup_workspace
    build_rpm

    [[ -n "${RPM_BUILD_DIR}" ]] && rm -rf "${RPM_BUILD_DIR}"
}

main $@
