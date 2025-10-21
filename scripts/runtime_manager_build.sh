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

YR_ROOT_DIR="${BASE_DIR}/../"
YUANRONG_DIR="${YR_ROOT_DIR}/../output/yuanrong"

DATA_SYSTEM_DIR="${YR_ROOT_DIR}/datasystem/output"

. "${YR_ROOT_DIR}"/tools/utils.sh

function copy_from_data_system()
{
  if [ ! -d "${DATA_SYSTEM_DIR}" ]; then
    log_error "data system dir is not exist"
    exit 1
  fi
  rm -rf "${YUANRONG_DIR}/data_system"
  mkdir -p "${YUANRONG_DIR}/data_system/service"
  cp -ar "${DATA_SYSTEM_DIR}/service/lib/" "${YUANRONG_DIR}/data_system/service"
  cp -ar "${DATA_SYSTEM_DIR}/service/datasystem_worker" "${YUANRONG_DIR}/data_system/service"
  cp -ar "${DATA_SYSTEM_DIR}/sdk" "${YUANRONG_DIR}/data_system/"
  rm -rf "${YUANRONG_DIR}/data_system/sdk/DATASYSTEM_SYM/"
  mkdir -p "${YUANRONG_DIR}/data_system/deploy"
  cp -ar "${BASE_DIR}/deploy/data_system"/* "${YUANRONG_DIR}/data_system/deploy"
}

function copy_module_runtime_manager() {
  log_info "copy module runtime-manager"

  if [ ! -d "${YUANRONG_DIR}/runtime/service/cpp/config/" ]; then
    mkdir -p "${YUANRONG_DIR}/runtime/service/cpp/config/"
  fi
  if [ -e "${BASE_DIR}/config/python_config/runtime.json" ]; then
    cp -ar "${BASE_DIR}"/config/python_config/runtime.json "${YUANRONG_DIR}/runtime/service/cpp/config/"
  fi

  if [ ! -d "${YUANRONG_DIR}/runtime/service/python/config/" ]; then
    mkdir -p "${YUANRONG_DIR}/runtime/service/python/config/"
  fi
  if [ -e "${BASE_DIR}/config/python_config/python-runtime-log.json" ]; then
    cp -ar "${BASE_DIR}"/config/python_config/python-runtime-log.json "${YUANRONG_DIR}/runtime/service/python/config/"
  fi

  copy_from_data_system
  log_info "copy module runtime-manager finished"
  return 0
}

copy_module_runtime_manager
