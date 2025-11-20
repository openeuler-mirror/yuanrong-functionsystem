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

function function_system_health_check() {
  local pid=$1
  local port=$2
  local dest=$3
  local addr="${IP_ADDRESS}:${port}"
  local node_id=${NODE_ID}
  local protocol="http"
  if ! kill -0 "${pid}" &>/dev/null; then
    # process not exist
    log_warning "process ${pid} is not exist"
    return 1
  fi
  local i
  local tls_opt=""
  if [ "X${SSL_ENABLE}" = "Xtrue" ]; then
      tls_opt="--cert ${CERTIFICATE_FILE_PATH} --key ${PRIVATE_KEY_PATH} --cacert ${VERIFY_FILE_PATH}"
      protocol="https"
  fi

  for ((i = 1; i <= FS_HEALTH_CHECK_RETRY_TIMES; i++)); do
    local ret_code=$(LD_LIBRARY_PATH="" timeout ${FS_HEALTH_CHECK_TIMEOUT} curl ${tls_opt} -s -m "${FS_HEALTH_CHECK_TIMEOUT}" -H "Node-ID:${NODE_ID}" -H "PID:${pid}" \
                         "${protocol}://${addr}/${dest}/healthy" -w %{http_code};echo $?)
    # ret_code长度为4时，一般前三位为curl返回的状态码，最后一位为curl退出码
    # ret_code长度为3时，一般表示curl执行超时（timeout命令返回124）
    if [ "x${ret_code:0:3}" = "x200" ]; then
      return 0
    fi

    if ! kill -0 "${pid}" &>/dev/null; then
      # process not exist
      log_warning "process ${pid} is not exist"
      return 1
    fi
    if [ $i -ge $FS_HEALTH_CHECK_RETRY_TIMES ]; then
      log_warning "${addr} health check exceed max retry times. code ${ret_code}"
      return 1
    fi
    sleep $FS_HEALTH_CHECK_RETRY_INTERVAL
  done
  return 1
}

function dashboard_health_check() {
  local pid=$1
  if ! kill -0 "${pid}" &>/dev/null; then
    # process not exist
    return 1
  fi
  return 0
}

function metaservice_health_check() {
  local pid=$1
  if ! kill -0 "${pid}" &>/dev/null; then
    # process not exist
    return 1
  fi
  return 0
}

function faas_frontend_health_check() {
  local pid=$1
  if ! kill -0 "${pid}" &>/dev/null; then
    # process not exist
    return 1
  fi
  return 0
}
