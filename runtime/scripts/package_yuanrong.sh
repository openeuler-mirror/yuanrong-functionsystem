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
. ${BASE_DIR}/package/utils.sh
OUTPUT_DIR="${BASE_DIR}/../output"
function parse_args () {
    getopt_cmd=$(getopt -o t:h -l tag:,help -- "$@")
    [ $? -ne 0 ] && exit 1
    eval set -- "$getopt_cmd"
    while true; do
        case "$1" in
        -h|--help) SHOW_HELP="true" && shift ;;
        -t|--tag) TAG=$2 && shift 2 ;;
        --) shift && break ;;
        *) die "Invalid option: $1" && exit 1 ;;
        esac
    done

    if [ "$SHOW_HELP" != "" ]; then
        cat <<EOF
Usage:
  packaging rpm packages, args and default values:
    -t|--tag             the version (=${TAG})
    -h|--help            show this help info
EOF
        exit 1
    fi
}

function get_all(){
  echo "download datasystem functionsystem"
  if [ -n "${FUNCTION_SYSTEM_CACHE}" ]; then
      curl -S -o functionsystem.tar.gz ${FUNCTION_SYSTEM_CACHE}
  fi
  if [ -n "${DATA_SYSTEM_CACHE}" ]; then
      curl -S -o datasystem.tar.gz ${DATA_SYSTEM_CACHE}
  fi
}

function main () {
    parse_args "$@"
}



main $@
rm -rf ${OUTPUT_DIR}/openyuanrong
mkdir -p ${OUTPUT_DIR}/openyuanrong
cd ${OUTPUT_DIR}

get_all

tar -zxvf yr-runtime-${TAG}.tar.gz -C ${OUTPUT_DIR}/openyuanrong
tar -zxvf functionsystem.tar.gz -C ${OUTPUT_DIR}/openyuanrong

mkdir -p ${OUTPUT_DIR}/openyuanrong/data_system
tar -zxvf datasystem.tar.gz -C ${OUTPUT_DIR}/openyuanrong/data_system
mkdir -p ${OUTPUT_DIR}/openyuanrong/data_system/deploy
cp -fr ${BASE_DIR}/../deploy/data_system/* ${OUTPUT_DIR}/openyuanrong/data_system/deploy/

cp -fr ${BASE_DIR}/../deploy ${OUTPUT_DIR}/openyuanrong
rm -rf ${OUTPUT_DIR}/openyuanrong/deploy/data_system

find . -type d -exec chmod 750 {} \;
find . -type l -exec chmod 777 {} \;
find . -type f -exec chmod 640 {} \;

if [ -d ${OUTPUT_DIR}/openyuanrong/deploy/process/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/deploy/process/ -type f -exec chmod 550 {} \;
  find ${OUTPUT_DIR}/openyuanrong/deploy/process/ -type f -name "*.yaml" -exec chmod 640 {} \;
fi

if [ -d ${OUTPUT_DIR}/openyuanrong/data_system/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/data_system/ -type f -exec chmod 550 {} \;
fi

mv ${OUTPUT_DIR}/openyuanrong/function_system/third_party ${OUTPUT_DIR}/openyuanrong/
if [ -d ${OUTPUT_DIR}/openyuanrong/third_party/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/third_party/ -type f -exec chmod 550 {} \;
fi

if [ -d ${OUTPUT_DIR}/openyuanrong/function_system/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/function_system/ -type f -exec chmod 550 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/function_system/config/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/function_system/config/ -type f -exec chmod 640 {} \;
fi

if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/deploy/process/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/deploy/process/ -type f -exec chmod 550 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/sdk/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/sdk/ -type f -exec chmod 550 {} \;
  find ${OUTPUT_DIR}/openyuanrong/runtime/sdk/ -type f -name "*.xml" -exec chmod 640 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/service/java/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/java/ -type f -exec chmod 550 {} \;
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/java/ -type f -name "*.xml" -exec chmod 640 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/service/cpp/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/cpp/ -type f -exec chmod 550 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/service/cpp/config/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/cpp/config/ -type f -exec chmod 640 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/service/python/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/python/ -type f -exec chmod 550 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/service/python/config/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/python/config/ -type f -exec chmod 640 {} \;
fi
if [ -d ${OUTPUT_DIR}/openyuanrong/runtime/service/python/yr/config/ ]; then
  find ${OUTPUT_DIR}/openyuanrong/runtime/service/python/yr/config/ -type f -exec chmod 640 {} \;
fi

cat >${OUTPUT_DIR}/openyuanrong/VERSION <<EOF
"${TAG}"
EOF
[ -d "${OUTPUT_DIR}/openyuanrong/runtime/sdk/cpp" ] && cp -ar ${OUTPUT_DIR}/openyuanrong/VERSION ${OUTPUT_DIR}/openyuanrong/runtime/sdk/cpp/VERSION

tar -zcf openyuanrong-${TAG}.tar.gz openyuanrong
