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

set -eu
set -o pipefail

install_dir=~/.cli
CLI_NAME=""

# clear and remove cli
[ -d "${install_dir}" ] && rm -rf ${install_dir}
mkdir -p ${install_dir}

files=$(ls ./cli/bin)
for filename in $files; do
    if [[ "$filename" != *"exe" ]]; then
        CLI_NAME=$filename
    fi
    break
done

if [ -z "$CLI_NAME" ]; then
    echo "there is no incorrect cli installation file"
    exit 1
fi

# install cli
cp -r ./cli/* ${install_dir}
if [ ! -d ~/bin ]; then
    mkdir ~/bin
fi

ln -sf ${install_dir}/bin/"${CLI_NAME}" ~/bin/"${CLI_NAME}"

echo -e "The CLI is installed successfully."
exit 0
