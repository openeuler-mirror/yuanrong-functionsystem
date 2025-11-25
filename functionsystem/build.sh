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
set -o nounset
set -o pipefail

readonly USAGE="
Usage: bash build.sh [-h] [-r] [-C] [-m <module name>] [-v <version>] [-j <job_num>] [-o <install dir>] [-S]
                     [-u off|build|run] [-M <test suit name>] [-t <test case name>] [-c off/on/html] [-O on/off]
                     [-P 1/2] [-k]

Options:
    -r specifies build type to 'Release' mode, default 'Debug' mode.
    -m compile the specific module, for example 'function_proxy', default is 'all'
    -v YuanRong version
    -V enable print ninja build verbose message
    -o set the output path of the compilation result, default is './output'.
    -j set the number of jobs run in parallel for compiling source code and compiling open source software,
       default is the number of processor.
    -C clear make history before building
    -O building with observability
    -h show usage
    -T set path of third party, default is ./third_party
    -P choose build part
    -k do not check charts
    -b enable print build object time trace

    For Debug Mode:
    -S Use Sanitizers tools to detect bugs. Choose from off/address/thread/leak,
       if set the value to 'address' enable AddressSanitizer, memory error detector
       if set the value to 'thread' enable ThreadSanitizer, data race detector.
       if set the value to 'memory' enable MemorySanitizer, a detector of uninitialized memory reads
       default off.

    For LLT:
    -c Build coverage, choose from: off/on/html, default: off.
    -M Specifies the test suit name to run, all suits by default.
    -t Specifies the testcase name to run, all testcases by default.
    -u Compiling or running unit testcases, default off. Choose from: off/build/run.
       Field 'off' indicates that testcases are not compiled and executed.
       Field 'build' indicates that testcases are compiled but not run.
       Field 'run' indicates that testcases are compiled and run.

    For tool:
    -g generate code from proto file.

Environment:
1) YR_OPENSOURCE_DIR: Specifies a directory to cache the opensource compilation result.
    Cache the compilation result to speed up the compilation. Default: readlink -f BASE_DIR

Example:
1) Compile a release version and export compilation result to the output directory.
  $ bash build.sh -r -m function_proxy -o ./output
"
BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)

# test options
BUILD_LLT=OFF
RUN_LLT=OFF
BUILD_GCOV=OFF
GEN_LLT_REPORT=OFF
UT_EXECUTABLE="functionsystem_unit_test"
IT_EXECUTABLE="functionsystem_integration_test"
TEST_SUIT="*"
TEST_CASE="*"
# compile options

YR_VERSION="yr-functionsystem-v0.0.1"
BUILD_TYPE=Debug
CLEAR_OUTPUT=OFF
SANITIZERS=OFF
MODULE=''
MODULE_LIST=("function_master" "function_proxy" "function_agent")
PROJECT_DIR="${BASE_DIR}"
BUILD_DIR="${BASE_DIR}/build"
OUTPUT_DIR="${BASE_DIR}/output"
YR_ROOT_DIR="${BASE_DIR}/.."
POSIX_DIR="${BASE_DIR}/src/common/proto/posix"
PACKAGE_OUTPUT_DIR="${YR_ROOT_DIR}/output"
FUNCTION_SYSTEM_PACKAGE_DIR="${YR_ROOT_DIR}/output/function_system"
FUNCTIONCORE_DIR="${YR_ROOT_DIR}/functioncore/"
SYM_OUTPUT_DIR="${YR_ROOT_DIR}/output/sym"
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"
JOB_NUM="$(($(grep -c 'processor' /proc/cpuinfo) + 1))"
YR_OPENSOURCE_DIR=""
BUILD_RUNTIMES="all"
VERBOSE=""

FUNCTION_SYSTEM_BUILD_TIME_TRACE="OFF"
JEMALLOC_PROF_ENABLE="OFF"

BUILD_ROOT_DIR="$(readlink -f "${PROJECT_DIR}/..")"
BUILD_CONFIG_DIR="${BUILD_ROOT_DIR}/thirdparty"
THIRDPARTY_SRC_DIR="${BUILD_ROOT_DIR}/vendor/"
THIRDPARTY_INSTALL_DIR="${THIRDPARTY_SRC_DIR}/out"

BUILD_FUNCTIONCORE="OFF"
FUNCTIONCORE_SRC_DIR="${YR_ROOT_DIR}/functionsystem"
FUNCTIONCORE_OUT_DIR="${YR_ROOT_DIR}/output/functioncore"
YUANRONG_DIR="${YR_ROOT_DIR}/../output/yuanrong"

# go module prepare
export GO111MODULE=on
export GONOSUMDB=*

. "${YR_ROOT_DIR}"/tools/utils.sh

if command -v ccache &> /dev/null
then
    ccache -p
    ccache -z
fi

usage_cpp() {
    echo -e "$USAGE"
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

function check_module() {
    if [[ "${MODULE_LIST[*]}" =~ $1 ]]; then
        return 0
    fi
    log_error "Invalid module name $1 for option -m"
    log_info "Valid module list: ${MODULE_LIST}"
    return 1
}

function check_posix() {
    log_info "Start check posix at ${POSIX_DIR}"
    if [ ! -d "${POSIX_DIR}" ]; then
        mkdir -p "${POSIX_DIR}"
    fi
    if [ ! -d "${YR_ROOT_DIR}/posix" ]; then
        log_error "The posix project does not exist, please check it"
        exit 1
    fi
    if [ ! -f "${POSIX_DIR}/common.proto" ]|| \
       [ ! -f "${POSIX_DIR}/core_service.proto" ]|| \
       [ ! -f "${POSIX_DIR}/runtime_rpc.proto" ]|| \
       [ ! -f "${POSIX_DIR}/affinity.proto" ]|| \
       [ ! -f "${POSIX_DIR}/runtime_service.proto" ]; then
        cp "${YR_ROOT_DIR}/posix/proto"/*.proto "${POSIX_DIR}"
        log_info "Get posix success"
    else
        log_info "posix file is exist"
    fi
}

function strip_symbols() {
    local src_dir="$1"
    local dest_dir="$2"
    if [[ ! -d "${dest_dir}" ]]; then
        mkdir -p "${dest_dir}"
    fi

    for file in ${src_dir}/*; do
        local type
        type="$(file -b --mime-type ${file} | sed 's|/.*||')"
        if [[ ! -L "${file}" ]] && [[ ! -d "${file}" ]] && [[ "x${type}" != "xtext" ]]; then
            strip_file_symbols ${file} ${dest_dir}
        fi
    done
}

function strip_file_symbols() {
    local file="$1"
    local dest_dir="$2"
    echo "---- start to strip ${file}"
    local basename
    basename=$(basename "${file}")
    objcopy --only-keep-debug "${file}" "${dest_dir}/${basename}.sym"
    objcopy --add-gnu-debuglink="${dest_dir}/${basename}.sym" "${file}"
    objcopy --strip-all "${file}"
}

function functioncore_compile() {
    log_info "functioncore build"
    if [ ! -d "${FUNCTIONCORE_SRC_DIR}" ]; then
        log_warning "functioncore is not existed, skip it"
        return 0
    fi
    cd "${FUNCTIONCORE_SRC_DIR}" && go mod tidy
    set +e
    GIT_BRANCH=$(git symbolic-ref --short -q HEAD)
    GIT_HASH=$(git log -1 "--pretty=format:[%H][%aI]")
    set -e
    log_info "version:${YR_VERSION} branch:${GIT_BRANCH} commitID:${GIT_HASH}"
    export GIT_HASH
    export GIT_BRANCH
    export YR_VERSION

    bash build_golang.sh linux || die "cli module build failed"
    CLI_NAME="yr" bash build_golang.sh linux || die "cli module build failed"

    rm -f "${PACKAGE_OUTPUT_DIR}"/${YR_VERSION}.tar.gz
    cd ${PACKAGE_OUTPUT_DIR}
    tar -czf "${PACKAGE_OUTPUT_DIR}"/${YR_VERSION}.tar.gz function_system
    rm -rf function_system
    cd ${YR_ROOT_DIR}

    log_info "functioncore build successfully"
}

while getopts 'yghrxVbm:v:o:j:S:Cc:u:t:M:d:T:s:R:P:p:k' opt; do
    case "$opt" in
    v)
        VERSION="${OPTARG}"
        YR_VERSION="yr-functionsystem-v${VERSION}"
        ;;
    V)
        VERBOSE="-v"
        ;;
    r)
        BUILD_TYPE=Release
        ;;
    o)
        OUTPUT_DIR=$(realpath -m "${OPTARG}")
        ;;
    j)
        check_number "${OPTARG}" j
        if [ ${OPTARG} -gt $(($CPU_NUM * 2)) ]; then
            log_warning "The -j $OPTARG is over the max logical cpu count($CPU_NUM) * 2"
        fi
        JOB_NUM="${OPTARG}"
        ;;
    h)
        usage_cpp
        exit 0
        ;;
    S)
        BUILD_TYPE=Debug
        SANITIZERS="${OPTARG}" # Debug工具
        ;;
    C)
        CLEAR_OUTPUT=ON
        ;;
    c)
        if [ "X${OPTARG}" = "Xoff" ]; then
            log_info "Coverage reports is disabled"
        elif [ "X${OPTARG}" = "Xon" ]; then
            BUILD_GCOV=ON
        elif [ "X${OPTARG}" = "Xhtml" ]; then
            BUILD_GCOV=ON
            GEN_LLT_REPORT=ON
        else
            log_error "Invalid value ${OPTARG} for option -c, choose from off/on/html"
            log_info "${USAGE}"
            exit 1
        fi
        ;;
    p)
        JEMALLOC_PROF_ENABLE=${OPTARG}
        ;;
    M)
        TEST_SUIT=${OPTARG}
        ;;
    t)
        TEST_CASE=${OPTARG}
        ;;
    u)
        if [ "X${OPTARG}" = "Xoff" ]; then
            log_info "LLT is disabled"
        elif [ "X${OPTARG}" = "Xbuild" ]; then
            BUILD_LLT=ON
        elif [ "X${OPTARG}" = "Xrun" ]; then
            BUILD_LLT=ON
            RUN_LLT=ON
        else
            log_error "Invalid value ${OPTARG} for option -u, choose from off/build/run"
            log_info "${USAGE}"
            exit 1
        fi
        ;;
    b)
        FUNCTION_SYSTEM_BUILD_TIME_TRACE=ON
        log_info "cmake build time trace is enabled"
        ;;
    y)
        BUILD_FUNCTIONCORE=ON
        log_info "build functioncore"
        ;;
    *)
        log_error "Invalid command"
        usage_cpp
        exit 1
        ;;
    esac
done

log_info "Begin to build, Build-Type:${BUILD_TYPE} Enable-LLT:${BUILD_LLT} Sanitizers:${SANITIZERS}"

if [ X"${BUILD_FUNCTIONCORE}" == X"ON" ]; then
    functioncore_compile
    exit 0
fi

function clear_object_posix() {
    local pb_object="${PROJECT_DIR}/src/common/proto/pb"
    [ -d "${pb_object}" ] && rm -f "${pb_object}"/*.pb.*

    local posix_objext="${pb_object}/posix"
    [ -d "${posix_objext}" ] && rm -rf "${posix_objext}"
}

if [ "X${CLEAR_OUTPUT}" = "XON" ]; then
    [ -d "${BUILD_DIR}" ] && rm -rf "${BUILD_DIR}"
    [ -d "${OUTPUT_DIR}" ] && rm -rf "${OUTPUT_DIR}"
    [ -d "${THIRDPARTY_SRC_DIR}" ] && rm -rf "${THIRDPARTY_SRC_DIR}"
    [ -d "${THIRDPARTY_INSTALL_DIR}" ] && rm -rf "${THIRDPARTY_INSTALL_DIR}"

    clear_object_posix
fi

# Check and get Posix
check_posix

# Build and install
echo cmake -G Ninja "${PROJECT_DIR}" -DCMAKE_INSTALL_PREFIX="${OUTPUT_DIR}" \
    -DBUILD_VERSION="${YR_VERSION}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DSANITIZERS="${SANITIZERS}" \
    -DBUILD_LLT="${BUILD_LLT}" \
    -DBUILD_GCOV="${BUILD_GCOV}" \
    -DBUILD_THREAD_NUM="${JOB_NUM}" \
    -DROOT_DIR="${YR_ROOT_DIR}" \
    -DJEMALLOC_PROF_ENABLE="${JEMALLOC_PROF_ENABLE}" \
    -DFUNCTION_SYSTEM_BUILD_TIME_TRACE="${FUNCTION_SYSTEM_BUILD_TIME_TRACE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"/
cmake -G Ninja "${PROJECT_DIR}" -DCMAKE_INSTALL_PREFIX="${OUTPUT_DIR}" \
    -DBUILD_VERSION="${YR_VERSION}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DSANITIZERS="${SANITIZERS}" \
    -DBUILD_LLT="${BUILD_LLT}" \
    -DBUILD_GCOV="${BUILD_GCOV}" \
    -DBUILD_THREAD_NUM="${JOB_NUM}" \
    -DROOT_DIR="${YR_ROOT_DIR}" \
    -DJEMALLOC_PROF_ENABLE="${JEMALLOC_PROF_ENABLE}" \
    -DFUNCTION_SYSTEM_BUILD_TIME_TRACE="${FUNCTION_SYSTEM_BUILD_TIME_TRACE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
echo "cmake configure successfully"

# Compatible with EulerOS and Ubuntu
if [ ! -d "${OUTPUT_DIR}"/lib64 ]; then
    mkdir -p "${OUTPUT_DIR}"/lib64
fi

if [ ! -d "${OUTPUT_DIR}"/lib ]; then
    mkdir -p "${OUTPUT_DIR}"/lib
fi

# If LLT is enabled to build, need to set LD_LIBRARY_PATH to link the generated shared library.
if [ "X${BUILD_LLT}" = "XON" ]; then
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH-}:${OUTPUT_DIR}/lib:${OUTPUT_DIR}/lib64"
    export BIN_PATH="${OUTPUT_DIR}/bin" # Integration test need this path to find test executables.
    export NOT_SKIP_LONG_TESTS=0 # skip long test case in CI
fi

echo "Start to compile ${MODULE} module"
# 编译指定组件，同时可以附加参数
ninja ${MODULE} ${VERBOSE} -j "${JOB_NUM}" || die "Failed to compile ${MODULE}"
echo "ninja compile successfully"
cmake --build ${BUILD_DIR} --target install || die "Failed to install ${MODULE}"
echo "cmake install successfully"

if [ "$BUILD_TYPE" != "Debug" ]; then
    [ ! -d "${OUTPUT_DIR}/functionsystem_SYM" ] && mkdir -p "${OUTPUT_DIR}/functionsystem_SYM"
    rm -rf "${OUTPUT_DIR}/functionsystem_SYM/*"
    strip_symbols "$OUTPUT_DIR"/bin "${OUTPUT_DIR}/functionsystem_SYM"
    strip_symbols "$OUTPUT_DIR"/lib "${OUTPUT_DIR}/functionsystem_SYM"
fi

if command -v ccache &> /dev/null
then
    ccache -s
fi

# Tests
if [ "X${RUN_LLT}" = "XON" ]; then
    pushd "bin"

    log_info "Run ut/it test units by python"
    PY_PATH=${YR_ROOT_DIR}/scripts/executor/run_code_gate.py
    IT_BIN_PATH=${BASE_DIR}/build/bin/${IT_EXECUTABLE}
    UT_BIN_PATH=${BASE_DIR}/build/bin/${UT_EXECUTABLE}
    python3 ${PY_PATH} --it_bin ${IT_BIN_PATH} --ut_bin ${UT_BIN_PATH} --test_suite "${TEST_SUIT}" --test_case "${TEST_CASE}"
    EXIT_CODE=$?
    echo "Run run_test_unit.py exit with code $EXIT_CODE"
    if [ ${EXIT_CODE} -ne 0 ]; then
      echo "Run run_test_unit.py with filter ${TEST_SUIT}.${TEST_CASE} filed."
      exit 1
    fi

    popd
fi

if [ "X${GEN_LLT_REPORT}" = "XON" ]; then
    log_info "Generate test report"
    coverage_file=coverage.info
    find ./src -type f -name "*.gcda" -printf '%h\n' | sort | uniq | xargs -P ${JOB_NUM} -I {} sh -c 'lcov -c -d "{}" -o coverage_$(echo "{}" | sed "s/\//_/g").part_tmp'
    lcov_files=$(find . -name "coverage_*.part_tmp" -size +0)
    lcov_files_with_a=$(echo "$lcov_files" | xargs -n 1 echo -a)
    lcov $lcov_files_with_a -o ${coverage_file}_tmp

    shielded_files=("*/common/utils/metadata/metadata.h"
                    "*/common/scheduler_framework/plugins/v1/preallocated_context.h" "*/common/utils/path.h"
                    "*/common/utils/files.h" "*/common/utils/actor_driver.h"
                    "*/common/utils/generate_message.h" "*/common/utils/hex.h"
                    "*/common/utils/param_check.h" "*/common/utils/proc_fs_tools.h" "*/common/utils/raii.h"
                    "*/common/utils/ssl_config.h" "*/common/utils/struct_transfer.h" "*/common/utils/ecdh_generator.cpp"
                    "*/common/utils/exec_utils.h" "*/common/utils/capability.h" "*/common/utils/cmd_tool.h"
                    "*/common/schedule_decision/queue/queue_item.h" "*/common/leader/leader_actor.h"
                    "*/common/utils/actor_worker.h" "*/common/meta_store/client/cpp/include/meta_store_client/txn_transaction.h"
                    "*/common/resource_view/resource_tool.h" "*/common/resource_view/scala_resource_tool.h"
                    "*/function_proxy/local_scheduler/local_sched_driver.cpp"
                    "*/function_proxy/busproxy/instance_proxy/perf.h"
                    "*/function_agent/code_deployer/obs_wrapper.h"
                    "*/common/scheduler_framework/utils/label_affinity_selector.h"
                    "*/common/scheduler_framework/framework/policy.h"
                    "*/output/include/metrics/*") # Coverage needs to be supplemented later.
    lcov -r ${coverage_file}_tmp "*vendor*" "*logs*" "*.3rd*" "*usr*" "*.pb.*" "*/build/*" "*litebus*" "metrics" "*datasystem/output/sdk/cpp/include/*" \
    ${shielded_files[@]} -o ${coverage_file}
    genhtml ${coverage_file} -o "coverage_report"
    rm -f $(find . -name "coverage_*.part_tmp")
fi

# copy function system output
mkdir -p "${FUNCTION_SYSTEM_PACKAGE_DIR}"/bin "${FUNCTION_SYSTEM_PACKAGE_DIR}"/lib "${FUNCTION_SYSTEM_PACKAGE_DIR}"/include "${FUNCTION_SYSTEM_PACKAGE_DIR}"/deploy "${FUNCTION_SYSTEM_PACKAGE_DIR}"/config
cp -ar "$OUTPUT_DIR"/bin/* "${FUNCTION_SYSTEM_PACKAGE_DIR}"/bin

if [ $(ls -A "$OUTPUT_DIR"/lib | wc -w) -ne 0 ]; then
    cp -ar "$OUTPUT_DIR"/lib/* "${FUNCTION_SYSTEM_PACKAGE_DIR}"/lib
fi

if [ -f "$OUTPUT_DIR"/include ]; then
    cp -ar "$OUTPUT_DIR"/include/* "${FUNCTION_SYSTEM_PACKAGE_DIR}"/include
fi

cp -ar  "${YR_ROOT_DIR}"/scripts/deploy/function_system/* "${FUNCTION_SYSTEM_PACKAGE_DIR}"/deploy

# copy metrics config file
cp -ar "${YR_ROOT_DIR}"/scripts/config/metrics/metrics_config.json "${FUNCTION_SYSTEM_PACKAGE_DIR}"/config/

if [ "$BUILD_TYPE" != "Debug" ]; then
    rm -rf "${SYM_OUTPUT_DIR}/functionsystem_SYM"
    cp -ar "${OUTPUT_DIR}/functionsystem_SYM" "${SYM_OUTPUT_DIR}/"
    tar -czf "${PACKAGE_OUTPUT_DIR}"/sym.tar.gz -C "${SYM_OUTPUT_DIR}/" .
    rm -rf "${SYM_OUTPUT_DIR}/"
fi
