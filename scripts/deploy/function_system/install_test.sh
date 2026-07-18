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
