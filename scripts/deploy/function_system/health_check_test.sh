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

set -euo pipefail

BASE_DIR=$(
  cd "$(dirname "$0")"
  pwd
)

# shellcheck source=/dev/null
. "${BASE_DIR}/health_check.sh"

CAPTURE_FILE=""

function log_warning() {
  :
}

function sleep() {
  :
}

function kill() {
  return 0
}

function timeout() {
  local _
  _="$1"
  shift
  "$@"
}

function curl() {
  printf '%s\n' "$*" > "${CAPTURE_FILE}"
  printf '200'
}

function assert_contains() {
  local actual="$1"
  local expected="$2"
  if [[ "${actual}" != *"${expected}"* ]]; then
    echo "expected [${actual}] to contain [${expected}]" >&2
    return 1
  fi
}

function assert_not_contains() {
  local actual="$1"
  local unexpected="$2"
  if [[ "${actual}" == *"${unexpected}"* ]]; then
    echo "expected [${actual}] to not contain [${unexpected}]" >&2
    return 1
  fi
}

function reset_env() {
  if [ -n "${CAPTURE_FILE}" ] && [ -f "${CAPTURE_FILE}" ]; then
    rm -f "${CAPTURE_FILE}"
  fi
  CAPTURE_FILE=$(mktemp)
  IP_ADDRESS="10.0.0.1"
  NODE_ID="test-node"
  IAM_SERVER_PORT="31112"
  IAM_LOCAL_LISTEN_PORT="0"
  IAM_LOCAL_IP="127.0.0.1"
  IAM_SSL_ENABLE=""
  SSL_ENABLE="false"
  CERTIFICATE_FILE_PATH="/tmp/module.crt"
  PRIVATE_KEY_PATH="/tmp/module.key"
  VERIFY_FILE_PATH="/tmp/ca.crt"
  FS_HEALTH_CHECK_RETRY_TIMES=1
  FS_HEALTH_CHECK_TIMEOUT=1
  FS_HEALTH_CHECK_RETRY_INTERVAL=0
}

function test_iam_health_check_prefers_local_plaintext_listener() {
  reset_env
  IAM_LOCAL_LISTEN_PORT="31113"
  IAM_SSL_ENABLE="true"
  SSL_ENABLE="true"

  iam_server_health_check 12345

  local captured
  captured=$(cat "${CAPTURE_FILE}")
  assert_contains "${captured}" "http://127.0.0.1:31113/iam-server/healthy"
  assert_not_contains "${captured}" "--cert"
  assert_not_contains "${captured}" "--key"
  assert_not_contains "${captured}" "--cacert"
}

function test_iam_health_check_uses_tls_for_remote_tls_only_listener() {
  reset_env
  IAM_SSL_ENABLE="true"

  iam_server_health_check 12345

  local captured
  captured=$(cat "${CAPTURE_FILE}")
  assert_contains "${captured}" "https://10.0.0.1:31112/iam-server/healthy"
  assert_contains "${captured}" "--cert /tmp/module.crt"
  assert_contains "${captured}" "--key /tmp/module.key"
  assert_contains "${captured}" "--cacert /tmp/ca.crt"
}

test_iam_health_check_prefers_local_plaintext_listener
test_iam_health_check_uses_tls_for_remote_tls_only_listener

if [ -n "${CAPTURE_FILE}" ] && [ -f "${CAPTURE_FILE}" ]; then
  rm -f "${CAPTURE_FILE}"
fi
