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

# ----------------------------------------------------------------------
# description: This script is used for building LiteBus libs.
#              First, download and compile 3rd-party softwares which LiteBus depended on;
#              Second, compile LiteBus;
#              Third, copy the head files and libraries of 3rd and libprocess to the install/include and install/lib directories of current directory.
# ----------------------------------------------------------------------

set -e

#--------------Constant--------------
CUR_DIR=$(dirname $(readlink -f $0))
BUILD_DIR=${CUR_DIR}/build
TOP_DIR=${CUR_DIR}/..
OUTPUT_DIR=${TOP_DIR}/output
LOG_DIR=${CUR_DIR}/logs
LOG_FILE=${LOG_DIR}/build.log
#--------------Variable--------------
build_type="release"
build_branch="develop_rtos"
dependency_path=""
code_coverage="off"
static_lib="off"
build_asan="off"
build_testcase="on"
http_enabled="on"
udp_enabled="on"
ssl_enabled="on"
openssl_version="1.1.1"
libprocess_interwork_enabled="off"
bit_compile="-m64"

DOWNLOAD_OPENSRC="OFF"
YR_ROOT_DIR="$(readlink -f "${TOP_DIR}/../..")"
BUILD_CONFIG_DIR="${YR_ROOT_DIR}/thirdparty"
THIRDPARTY_SRC_DIR="${YR_ROOT_DIR}/vendor"
THIRDPARTY_INSTALL_DIR="${THIRDPARTY_SRC_DIR}/out"
CPU_NUM="$(grep -c 'processor' /proc/cpuinfo)"
JOB_NUM="$(($(grep -c 'processor' /proc/cpuinfo) + 1))"

# ----------------------------------------------------------------------
# funcname:     log_info.
# description:  Print build info log.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
log_info()
{
    echo "[$(date -u +%Y-%m-%d\ %H:%M:%S)] [Build] [Info] $@" | tee -a ${LOG_FILE}
}

# ----------------------------------------------------------------------
# funcname:     log_warnning.
# description:  Print build warning log.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
log_warnning()
{
    echo "[$(date -u +%Y-%m-%d\ %H:%M:%S)] [Build] [Warning] $@" | tee -a ${LOG_FILE}
}

# ----------------------------------------------------------------------
# funcname:     log_error.
# description:  Print build error log.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
log_error()
{
    echo "[$(date -u +%Y-%m-%d\ %H:%M:%S)] [Build] [Error] $@" | tee -a ${LOG_FILE}
}

# ----------------------------------------------------------------------
# funcname:     clean.
# description:  Clean temporary files.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
clean()
{
    [[ -d "${BUILD_DIR}" ]] && rm -rf "${BUILD_DIR}"
    [[ -d "${THIRDPARTY_SRC_DIR}" ]] && rm -rf "${THIRDPARTY_SRC_DIR}"
    [[ -d "${OUTPUT_DIR}" ]] && rm -rf "${OUTPUT_DIR}"

    [[ -d "${TOP_DIR}"/test/schema ]] && rm -rf "${TOP_DIR}"/test/schema/*.c
    [[ -d "${TOP_DIR}"/test/schema ]] && rm -rf "${TOP_DIR}"/test/schema/*.h

    log_info "clean package litebus success!"
}

# ----------------------------------------------------------------------
# funcname:     usage
# description:  print usage.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
usage()
{
    echo -e "Options:"
    echo -e "     -b build_branch. Specify the dependency package's branch on FTP, used for local building. Default:"develop_rtos" "
    echo -e "     -c code_coverage.  Enable code coverage or not. "on": enable coverage. Default:"off" "
    echo -e "     -p dependency_path.  Sepecify the dependency package's path, only used for version-building"
    echo -e "     -t build_testcase. Build testcase or not. "on": build testcases. Default: "on" "
    echo -e "     -H Enable http building. "on": build http. Default: "on" "
    echo -e "     -U Enable udp building. "on": build udp. Default: "on" "
    echo -e "     -S Enable ssl building. "on": build ssl. Default: "off" "
    echo -e "     -X openssl version. format is 'major[.minor[.patch[.tweak]]]'. Default: "1.1.1" "
    echo -e "     -W enable libprocess interwork tests. Default: "off" "
    echo -e "     -A Enable asan building"
    echo -e "     -D Enable debug mode"
    echo -e "     -s make static lib. "on": static. Default: "off" "
    echo -e "     -m 32/64 bit compile. Support 32 and 64. Default:64"
    echo -e "     -j set the number of jobs run in parallel for compiling source code and compiling open source software. Default: the number of processor(${CPU_NUM})"
    echo -e "      "
    echo -e "Example:"
    echo -e "     sh build.sh -b develop_rtos"
    echo -e ""
}

# ----------------------------------------------------------------------
# funcname:     checkopts
# description:  check options.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
checkopts()
{
    while getopts 'b:c:p:t:x:H:U:S:m:X:W:F:T:j:ADs' opt
    do
        case "$opt" in
        b)
            build_branch=$OPTARG
            HARS_LOG_INFO "build_branch=${build_branch}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
	    c)
            code_coverage=$OPTARG
            log_info "code_coverage=${code_coverage}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        p)
            dependency_path=$OPTARG
            log_info "dependency_path=${dependency_path}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        t)
            build_testcase=$OPTARG
            log_info "build_testcase=${build_testcase}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        H)
            http_enabled=$OPTARG
            log_info "http_enabled=${http_enabled}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        U)
            udp_enabled=$OPTARG
            log_info "udp_enabled=${udp_enabled}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        S)
            ssl_enabled=$OPTARG
            log_info "ssl_enabled=${ssl_enabled}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        m)
            if [ x"$OPTARG" = "x32" ]; then
                bit_compile="-m32"
            elif [ x"$OPTARG" = "x64" ]; then
                bit_compile="-m64"
            else
                log_error "-$OPTARG should be 32/64"
                exit 1
            fi
            log_info "bit_compile=${bit_compile}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        X)
            openssl_version=$OPTARG
            log_info "openssl_version=${openssl_version}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        W)
            libprocess_interwork_enabled=$OPTARG
            log_info "libprocess_interwork_enabled=${libprocess_interwork_enabled}"
            ;;
        :)
            log_error "-$OPTARG needs an argument"
            exit 1
            ;;
        A)
            build_asan="on"
            log_info "build_asan=${build_asan}"
            ;;
        D)
            build_type="debug"
            log_info "build_type=${build_type}"
            ;;
        s)
            static_lib="on"
            log_info "static_lib=${static_lib}"
            ;;
        x)
            if [ "${OPTARG^^}" = "ON" ]; then
                DOWNLOAD_OPENSRC="$OPTARG"
            fi
            ;;
        T)
            THIRDPARTY_SRC_DIR=$(readlink -f "${OPTARG}")
            THIRDPARTY_INSTALL_DIR="${THIRDPARTY_SRC_DIR}/out"
            echo "download opensource to ${THIRDPARTY_SRC_DIR}"
            ;;
        j)
            if [ ${OPTARG} -gt $(($CPU_NUM * 2)) ]; then
                log_warning "The -j $OPTARG is over the max logical cpu count($CPU_NUM) * 2"
            fi
            JOB_NUM="${OPTARG}"
            ;;
        *)
            echo "command not recognized"
            usage
            exit 1
            ;;
        esac
    done
}

# ----------------------------------------------------------------------
# funcname:     make_dir
# description:  make dir.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
make_dir()
{
    __dir__=$1
    if [ ! -d "${__dir__}" ]; then
        mkdir -p ${__dir__}
    else
        rm -rf ${__dir__}/*
    fi
}

# ----------------------------------------------------------------------
# funcname:     setup_dirs
# description:  setup dirs
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
setup_dirs()
{
    make_dir "${BUILD_DIR}"
    make_dir "${OUTPUT_DIR}"
}

# ----------------------------------------------------------------------
# funcname:     compile_litebus
# description:  compile_litebus.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
compile_litebus()
{
    MAKE_OPTS=""
    export IMPOSTER_COMPILER_ARG1=$(which gcc)
    export IMPOSTER_COMPILER_ARG2=$(which g++)

    cd ${BUILD_DIR}

    cmake ${CUR_DIR}/.. -DCMAKE_SKIP_RPATH=TRUE \
      -DCMAKE_TOOLCHAIN_FILE="${CUR_DIR}"/../cmake/x86_64_toolchain.cmake \
      -DCMAKE_INSTALL_PREFIX="${OUTPUT_DIR}" \
      -DROOT_DIR="${YR_ROOT_DIR}" \
      -DBUILD_CONFIG_DIR="${BUILD_CONFIG_DIR}" \
      -DTHIRDPARTY_SRC_DIR="${THIRDPARTY_SRC_DIR}" \
      -DTHIRDPARTY_INSTALL_DIR="${THIRDPARTY_INSTALL_DIR}" \
      -DCMAKE_BUILD_TYPE=${build_type} \
      -DCODE_COVERAGE=${code_coverage} \
      -DBUILD_TESTCASE=${build_testcase} \
      -DENABLE_ASAN=${build_asan} \
      -DHTTP_ENABLED=${http_enabled} \
      -DUDP_ENABLED=${udp_enabled} \
      -DSSL_ENABLED=${ssl_enabled} \
      -DLIBPROCESS_INTERWORK_ENABLED=${libprocess_interwork_enabled} \
      -DBIT_COMP_FLAGS=${bit_compile} \
      -DSTATIC_LIB=${static_lib}

    cmake --build ${BUILD_DIR} --parallel ${JOB_NUM} --verbose -- ${MAKE_OPTS}
    ret=$?

    if [ "${ret}" -ne 0 ]; then
        log_error "build fail!"
        exit 1
    fi

    cmake --build ${BUILD_DIR} --target install

    log_info "build litebus success!"
    cd ${CUR_DIR}
}

# ----------------------------------------------------------------------
# funcname:     compile_protocal_files
# description:  compile_protocal_files.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
compile_protocal_files()
{
  if [ "${build_testcase}" == "on" ];then
      echo "compile protocal files"
      echo ${CUR_DIR}
      export LD_LIBRARY_PATH=${THIRDPARTY_SRC_DIR}/PROTOBUF_C/lib/:${LD_LIBRARY_PATH}
      PROTOC_BIN_DIR=${THIRDPARTY_SRC_DIR}/PROTOBUF_C/bin/protoc-c

      PROTOC_TO_INCLUDE_DIR=${BUILD_DIR}/include
      make_dir ${PROTOC_TO_INCLUDE_DIR}

      PROTOC_SRC_DIR=${TOP_DIR}/test/schema

      $PROTOC_BIN_DIR -I${PROTOC_SRC_DIR} --c_out=${CUR_DIR}/../test/schema ${PROTOC_SRC_DIR}/clientserver.proto
      $PROTOC_BIN_DIR -I${PROTOC_SRC_DIR} --c_out=${CUR_DIR}/../test/schema ${PROTOC_SRC_DIR}/base.proto

      cd ${CUR_DIR}
  fi
}

# ----------------------------------------------------------------------
# funcname:     tar_files
# description:  tar_files.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
tar_files()
{
    cd ${OUTPUT_DIR}

    tar -zcvf LITEBUS.tar.gz include lib >/dev/null

    cd lib
    chmod 400 liblitebus.so.0.0.1
    cd ../

    cd ${CUR_DIR}

    log_info "package litebus success!"
}

# ----------------------------------------------------------------------
# funcname:     configure
# description:  configure compile environment.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
configure()
{
  echo "nothing to configure"
}

# ----------------------------------------------------------------------
# funcname:     init_log
# description:  initiate log file
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
init_log()
{
  if [ ! -d "${LOG_DIR}" ];then
    mkdir -p ${LOG_DIR}
  fi

  if [ -f "${LOG_FILE}" ];then
    rm -f ${LOG_FILE}
  fi

  touch ${LOG_FILE}
}

clean_log()
{
  rm -rf "${LOG_DIR}"/*.log
}

# ----------------------------------------------------------------------
# funcname:     compile
# description:  compile.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
compile()
{
    if command -v ccache &> /dev/null
    then
        ccache -p
        ccache -z
    fi
    configure
    compile_litebus
    if command -v ccache &> /dev/null
    then
        ccache -s
    fi
}

# ----------------------------------------------------------------------
# funcname:     copy_files
# description:  copy_files.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
copy_files()
{
    tar_files
}

# ----------------------------------------------------------------------
# funcname:     download_opensource
# description:  download opensource.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
download_opensource()
{
    if [ "${DOWNLOAD_OPENSRC^^}" != "ON" ]; then
        echo "don't need download opensource"
        return 0
    fi

    echo "yr root dir: ${YR_ROOT_DIR}"
    echo "build config dir: ${BUILD_CONFIG_DIR}"
    echo "thirdparty src dir: ${THIRDPARTY_SRC_DIR}"
    echo "thirdparty install dir: ${THIRDPARTY_INSTALL_DIR}"

    if [ ! -d "${BUILD_CONFIG_DIR}" ]; then
        echo "please download build config"
        exit 1
    fi

    ARGS="-T ${THIRDPARTY_SRC_DIR} -Y ${YR_ROOT_DIR} -M litebus"
    if [ "X${build_testcase}" = "Xoff" ]; then
        ARGS="${ARGS} -r"
    fi

    if ! bash "${BUILD_CONFIG_DIR}/download_src.sh" "${ARGS}"; then
        echo "download dependency source of src fail"
        exit 1
    fi
}


# ----------------------------------------------------------------------
# funcname:     build
# description:  build.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
build()
{
    checkopts $@

    download_opensource
    setup_dirs
    compile
    copy_files
}

# ----------------------------------------------------------------------
# funcname:     main
# description:  main.
# parameters:   NA
# return value: NA
# ----------------------------------------------------------------------
main()
{
    if [ "X$1" == "Xclean" ];then
      clean
      clean_log
      exit 0
    fi

    init_log
    build $@
}

main $@
exit 0

