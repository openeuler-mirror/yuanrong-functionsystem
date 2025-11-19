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

source /etc/profile.d/*.sh
BASE_DIR=$(dirname "$(readlink -f "$0")")
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"

. "${BASE_DIR}"/config.sh
. "${BASE_DIR}"/tools/utils.sh

function thirdparty_compile() {
  python ${BASE_DIR}/scripts/executor/download_opensource.py --config ${BASE_DIR}/vendor/ThirdPartyList.csv --output ${BASE_DIR}/vendor/src
  cd "${BASE_DIR}"/vendor
  cmake -B build
  cmake --build build --parallel ${JOB_NUM}

  # compile thirdparty: etcd
  cd "${BASE_DIR}"/scripts/
  bash basic_build.sh || die "etcd build failed"
}

function logs_compile() {
  echo "Compile common/logs"
  cd "${BASE_DIR}"/common/logs
  bash build.sh -j "${JOB_NUM}"
}

function litebus_compile() {
  echo "Compile common/litebus"
  cd "${BASE_DIR}"/common/litebus
  bash build.sh -t off -j "${JOB_NUM}"
}

function metrics_compile() {
  echo "Compile common/metrics"
  cd "${BASE_DIR}"/common/metrics
  bash build.sh -j "${JOB_NUM}"
}

function functionsystem_compile() {
  cd "${BASE_DIR}"/functionsystem
  # compile functionsystem
  bash build.sh -r -j "${JOB_NUM}" -v "yr-functionsystem-v${YR_FUNCTION_SYSTEM_VERSION}"

  # compile functioncore
  bash build.sh -y -j "${JOB_NUM}" -v "yr-functionsystem-v${YR_FUNCTION_SYSTEM_VERSION}"

  unset CMC_USERNAME
  unset CMC_PASSWORD
}

function package() {
  bash "${BASE_DIR}"/scripts/package/package.sh $@
}

function clean_build_cache() {
  echo "clean vendor cache and component build cache"
  cd "${BASE_DIR}" && git clean -dffx
  git submodule foreach --recursive git clean -dffx
}

function clean() {
  [[ -n "${OUTPUT_DIR}" ]] && rm -rf "${OUTPUT_DIR}"
}

function doc_build() {
  pushd ${BASE_DIR}
  pip install -r runtime/api/python/requirements.txt
  pip install -r docs/requirements_dev.txt
  popd

  pushd ${BASE_DIR}/docs
  make html
  # disable configuration：SPHINXOPTS="-W --keep-going -n", enable it after all alarms are cleared.
  popd

  # modify sphinx(7.3.7) build-in search，open limit on numbers in search.
  # Changes in later versions need to be modified accordingly.
  sed -i '285d' "${BASE_DIR}"/docs/_build/html/_static/searchtools.js
  sed -i '284s/ ||//' "${BASE_DIR}"/docs/_build/html/_static/searchtools.js
  rm -rf "${OUTPUT_DIR}"/docs && mkdir -p "${OUTPUT_DIR}"/docs
  cp -rf "${BASE_DIR}"/docs/_build/html/* "${OUTPUT_DIR}"/docs
}

function check_number() {
    number_check='^([0-9]+)$'
    if [[ "$1" =~ ${number_check} ]]; then
        return 0
    else
        log_error "Invalid value $1 for option -$2"
        log_warning "${USAGE}"
        exit 1
    fi
}

function build() {
  while getopts 'j:v:hc' opt; do
      case "$opt" in
      j)
          check_number "${OPTARG}" j
          if [ ${OPTARG} -gt $(($CPU_NUM * 2)) ]; then
              log_warning "The -j $OPTARG is over the max logical cpu count($CPU_NUM) * 2"
          fi
          JOB_NUM="${OPTARG}"
          ;;
      v)
          YR_FUNCTION_SYSTEM_VERSION=${OPTARG}
          ;;
      h)
          echo -e $USAGE
          exit 0
          ;;
      c)
          clean_build_cache
          exit 0
          ;;
      *)
          log_error "Invalid command"
          exit 1
          ;;
      esac
  done

  log_info "compile functionsystem"
  thirdparty_compile
  logs_compile
  litebus_compile
  metrics_compile
  functionsystem_compile
}

# provide subcommands so we can support more scenario
case "$1" in
  clean)
    shift
    clean $@
    clean_build_cache
    ;;
  build)
    shift
    build $@
    ;;
  package)
    shift
    package $@
    ;;
  doc-build)
    shift
    doc_build $@
    ;;
  *)
    log_info "Using default subcommand: build"
    clean
    build $@
    ;;
esac
