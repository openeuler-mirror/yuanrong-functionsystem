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

readonly USAGE="
Usage: bash build.sh [-thdDcCrvPSbEm:]

Options:
    -t run test.
    -v the version of yuanrong
    -c coverage
    -C clean build environment
    -p the specified version of python runtime
    -o specify the output directory
    -h usage.
"

BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)

BUILD_DIR=$BASE_DIR/build
OUTPUT_DIR=$BASE_DIR/output
COMMAND="build"
BUILD_VERSION="0.0.1"
PYTHON3_BIN_PATH="python3"

usage() {
    echo -e "$USAGE"
}

while getopts 'tv:cCp:o:h' opt; do
    case "$opt" in
    t)
        COMMAND="test"
        ;;
    v)
        BUILD_VERSION="${OPTARG}"
        export BUILD_VERSION="${OPTARG}"
        ;;
    c)
        COMMAND="coverage"
        ;;
    C)
        COMMAND="clean"
        ;;
    p)
        PYTHON3_BIN_PATH="${OPTARG}"
        ;;
    o)
        OUTPUT_DIR=$(readlink -f "${OPTARG}")
        ;;
    h)
        usage
        exit 0
        ;;
    *)
        log_error "invalid command: $opt"
        usage
        exit 1
        ;;
    esac
done

if [ $COMMAND == "build" ]; then
    export BUILD_VERSION
    $PYTHON3_BIN_PATH setup.py bdist_wheel -b $BUILD_DIR -d $OUTPUT_DIR
elif [ $COMMAND == "test" ]; then
    echo "not support"
elif [ $COMMAND == "coverage" ]; then
    echo "not support"
elif [ $COMMAND == "clean" ]; then
    python setup.py clean
    rm -rf $BUILD_DIR
    rm -rf $OUTPUT_DIR
fi
