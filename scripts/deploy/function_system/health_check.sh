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

function is_true() {
  [ "X${1}" = "Xtrue" ] || [ "X${1}" = "XTRUE" ]
}

function function_system_health_check_with_addr() {
  local pid=$1
  local addr=$2
  local dest=$3
  local protocol=$4
  shift 4
  local curl_args=("$@")
  if ! kill -0 "${pid}" &>/dev/null; then
    # process not exist
    log_warning "process ${pid} is not exist"
    return 1
  fi
  local i
  for ((i = 1; i <= FS_HEALTH_CHECK_RETRY_TIMES; i++)); do
    local ret_code
    if [ ${#curl_args[@]} -gt 0 ]; then
      ret_code=$(LD_LIBRARY_PATH="" timeout ${FS_HEALTH_CHECK_TIMEOUT} curl "${curl_args[@]}" -s \
                     -m "${FS_HEALTH_CHECK_TIMEOUT}" -H "Node-ID:${NODE_ID}" -H "PID:${pid}" \
                     "${protocol}://${addr}/${dest}/healthy" -w %{http_code}; echo $?)
    else
      ret_code=$(LD_LIBRARY_PATH="" timeout ${FS_HEALTH_CHECK_TIMEOUT} curl -s -m "${FS_HEALTH_CHECK_TIMEOUT}" \
                     -H "Node-ID:${NODE_ID}" -H "PID:${pid}" "${protocol}://${addr}/${dest}/healthy" \
                     -w %{http_code}; echo $?)
    fi
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

function function_system_health_check() {
  local pid=$1
  local port=$2
  local dest=$3
  local tls_var_name="${4:-}"
  local local_port_var_name="${5:-}"
  local local_ip_var_name="${6:-}"
  local addr="${IP_ADDRESS}:${port}"
  local protocol="http"
  local curl_args=()
  local tls_enable="${SSL_ENABLE}"
  local local_port=""
  local local_ip=""
  if [ -n "${local_port_var_name}" ]; then
    local_port="${!local_port_var_name:-}"
  fi
  if [ -n "${local_port}" ] && [ "${local_port}" != "0" ]; then
    if [ -n "${local_ip_var_name}" ]; then
      local_ip="${!local_ip_var_name:-127.0.0.1}"
    else
      local_ip="127.0.0.1"
    fi
    addr="${local_ip}:${local_port}"
  else
    if [ -n "${tls_var_name}" ] && [ -n "${!tls_var_name:-}" ]; then
      tls_enable="${!tls_var_name}"
    fi
    if is_true "${tls_enable}"; then
      curl_args=(--cert "${CERTIFICATE_FILE_PATH}" --key "${PRIVATE_KEY_PATH}" --cacert "${VERIFY_FILE_PATH}")
      protocol="https"
    fi
  fi
  if [ ${#curl_args[@]} -gt 0 ]; then
    function_system_health_check_with_addr "${pid}" "${addr}" "${dest}" "${protocol}" "${curl_args[@]}"
  else
    function_system_health_check_with_addr "${pid}" "${addr}" "${dest}" "${protocol}"
  fi
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
