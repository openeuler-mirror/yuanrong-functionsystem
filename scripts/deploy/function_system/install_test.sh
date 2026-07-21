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
