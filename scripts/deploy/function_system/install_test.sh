#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

install_script="${BASE_DIR}/install.sh"
merged_agent_body=$(sed -n \
  '/^function install_function_agent_and_runtime_manager_in_the_same_process()/,/^function install_function_master()/p' \
  "${install_script}")
expected='--runtime_ld_library_path="${ld_library_path}:${RUNTIME_HOME_DIR}/service/cpp/snlib:${RUNTIME_HOME_DIR}/sdk/cpp/lib"'

if [[ "${merged_agent_body}" != *"${expected}"* ]]; then
  echo "merged function_agent must forward runtime_ld_library_path" >&2
  exit 1
fi

proxy_body=$(sed -n \
  '/^function install_function_proxy()/,/^function install_dashboard()/p' \
  "${install_script}")
expected_data_system_arg='--data_system_enable="${DATA_SYSTEM_ENABLE:-false}"'
for launch_body in "${proxy_body}" "${merged_agent_body}"; do
  if [[ "${launch_body}" != *"${expected_data_system_arg}"* ]]; then
    echo "function_agent must forward DATA_SYSTEM_ENABLE" >&2
    exit 1
  fi
  if [[ "${launch_body}" == *'--data_system_enable=true'* ]]; then
    echo "function_agent must not hard-code data_system_enable" >&2
    exit 1
  fi
done

test_tmp_dir=$(mktemp -d)
trap 'rm -rf "${test_tmp_dir}"' EXIT
scheduler_config="${test_tmp_dir}/init_scheduler_args.json"

cat >"${scheduler_config}" <<'EOF'
{
  "liteScheduler": {
    "enable": {liteEnable},
    "enableAllTenants": {liteEnableAllTenants},
    "enabledTenants": {liteEnabledTenants},
    "enabledFunctions": {liteEnabledFunctions},
    "acquireWaitTimeoutMs": {liteAcquireWaitTimeoutMs}
  }
}
EOF

LITE_SCHEDULER_ENABLE=true
LITE_SCHEDULER_ENABLE_ALL_TENANTS=false
LITE_SCHEDULER_ENABLED_TENANTS='["tenant-a","tenant-b"]'
LITE_SCHEDULER_ENABLED_FUNCTIONS='["0-defaultservice-rrt"]'
LITE_SCHEDULER_ACQUIRE_WAIT_TIMEOUT_MS=3000

scheduler_body=$(sed -n \
  '/^function install_function_scheduler()/,/^function install_function_agent_and_runtime_manager_in_the_same_process()/p' \
  "${install_script}")
frontend_body=$(sed -n \
  '/^function install_faas_frontend()/,/^function install_function_scheduler()/p' \
  "${install_script}")

if [[ "${frontend_body}" != *'init_frontend_config=$(resolve_faas_config_path "init_frontend_args.json")'* ]]; then
  echo "FaaS frontend must resolve configs from full and core wheel layouts" >&2
  exit 1
fi
if [[ "${scheduler_body}" != *'init_scheduler_config=$(resolve_faas_config_path "init_scheduler_args.json")'* ]]; then
  echo "function scheduler must resolve configs from full and core wheel layouts" >&2
  exit 1
fi

if [[ "${scheduler_body}" == *"render_lite_scheduler_config"* ]]; then
  echo "LiteScheduler config must follow the existing inline scheduler rendering pattern" >&2
  exit 1
fi
if printf '%s\n' "${scheduler_body}" | grep -Eq '\$\{LITE_SCHEDULER_[^}]*:-'; then
  echo "LiteScheduler defaults must only be defined in config.sh" >&2
  exit 1
fi

inline_renderer_body=$(printf '%s\n' "${scheduler_body}" | sed -n \
  -e '/lite_enabled_tenants=/p' \
  -e '/lite_enabled_functions=/p' \
  -e '/{liteEnable}/p' \
  -e '/{liteEnableAllTenants}/p' \
  -e '/{liteEnabledTenants}/p' \
  -e '/{liteEnabledFunctions}/p' \
  -e '/{liteAcquireWaitTimeoutMs}/p')
install_init_scheduler_config=${scheduler_config}
eval "${inline_renderer_body}"

python3 - "${scheduler_config}" <<'PY'
import json
import pathlib
import sys

config_path = pathlib.Path(sys.argv[1])
text = config_path.read_text()
if "{lite" in text:
    raise AssertionError(f"LiteScheduler placeholder remains in {text}")

lite = json.loads(text)["liteScheduler"]
assert lite == {
    "enable": True,
    "enableAllTenants": False,
    "enabledTenants": ["tenant-a", "tenant-b"],
    "enabledFunctions": ["0-defaultservice-rrt"],
    "acquireWaitTimeoutMs": 3000,
}, lite
PY

function readlink() {
  if [ "${1:-}" = "-m" ]; then
    shift
    python3 - "$1" <<'PY'
import os
import sys

print(os.path.realpath(sys.argv[1]))
PY
    return
  fi
  command readlink "$@"
}

function assert_layout_resolution() (
  local layout_root="$1"
  local expected_faas_root="$2"
  local expected_config_root="$3"

  BASE_DIR="${layout_root}/yr/deploy/process"
  RUNTIME_HOME_DIR=""
  FUNCTION_META_PATH=""
  # shellcheck source=/dev/null
  . "${install_script}"

  if [ "${PATTERN_FAAS_HOME_DIR}" != "${expected_faas_root}" ]; then
    echo "expected FaaS root ${expected_faas_root}, got ${PATTERN_FAAS_HOME_DIR}" >&2
    exit 1
  fi
  if [ "${FUNCTION_META_PATH}" != "${expected_faas_root}/executor-meta" ]; then
    echo "unexpected function meta path: ${FUNCTION_META_PATH}" >&2
    exit 1
  fi

  local frontend_config
  local scheduler_config_path
  frontend_config=$(resolve_faas_config_path "init_frontend_args.json")
  scheduler_config_path=$(resolve_faas_config_path "init_scheduler_args.json")
  if [ "${frontend_config}" != "${expected_config_root}/init_frontend_args.json" ]; then
    echo "unexpected frontend config path: ${frontend_config}" >&2
    exit 1
  fi
  if [ "${scheduler_config_path}" != "${expected_config_root}/init_scheduler_args.json" ]; then
    echo "unexpected scheduler config path: ${scheduler_config_path}" >&2
    exit 1
  fi
)

full_root="${test_tmp_dir}/full"
full_faas_root="${full_root}/yr/pattern/pattern_faas"
full_config_root="${full_root}/yr/functionsystem/config"
mkdir -p \
  "${full_root}/yr/deploy/process" \
  "${full_root}/yr/faas" \
  "${full_faas_root}/executor-meta" \
  "${full_config_root}"
touch \
  "${full_config_root}/init_frontend_args.json" \
  "${full_config_root}/init_scheduler_args.json"
assert_layout_resolution "${full_root}" "${full_faas_root}" "${full_config_root}"

core_root="${test_tmp_dir}/core"
core_faas_root="${core_root}/yr/faas"
mkdir -p \
  "${core_root}/yr/deploy/process" \
  "${core_root}/yr/functionsystem/config" \
  "${core_faas_root}/executor-meta"
touch \
  "${core_faas_root}/init_frontend_args.json" \
  "${core_faas_root}/init_scheduler_args.json"
assert_layout_resolution "${core_root}" "${core_faas_root}" "${core_faas_root}"

if [ -e "${core_root}/yr/pattern" ]; then
  echo "core wheel compatibility must not create a legacy layout" >&2
  exit 1
fi
