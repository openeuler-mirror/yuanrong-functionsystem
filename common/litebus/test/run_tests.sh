#!/bin/bash

CUR_DIR=$(dirname $(readlink -f $0))
TOP_DIR=${CUR_DIR}/..
TEST_MODEL="$1"

#Unzip LiteBus.tar.gz to use libhttp_parser.so
cd "${TOP_DIR}"/output || exit
tar xvf LITEBUS.tar.gz

SSL_UTILS_PATH="${CUR_DIR}/ssl_utils"
if [ ! -d "${SSL_UTILS_PATH}" ]; then
  echo "litebus-test pull ssl_utils!"
  cd "${CUR_DIR}" || exit
  if [ -z "${SSL_UTILS_ADDR}" ]; then
    echo -e "SSL_UTILS download address must be configured in advance!"
    exit 1
  fi
  curl -S -O ${SSL_UTILS_ADDR} -o ssl_utils.tar.gz
  tar -zxvf ssl_utils.tar.gz
  rm -rf ssl_utils.tar.gz
  cd - || exit
fi

export LITEBUS_SSL_SANDBOX="${SSL_UTILS_PATH}/"

export LD_LIBRARY_PATH=${TOP_DIR}/build/build/src:${TOP_DIR}/output/lib:${LD_LIBRARY_PATH}

export LIBPROCESS_GLOG_PATH=${TOP_DIR}/build/3rdparty/LIBPROCESS/lib


echo "LITEBUS_SSL_SANDBOX: ${LITEBUS_SSL_SANDBOX}"
echo "LIBPROCESS_GLOG_PATH: ${LIBPROCESS_GLOG_PATH}"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"

export LITEBUS_HTTPKMSG_ENABLED=0 # why do this?
cd "${TOP_DIR}"/build/build/test || exit
if [ ! -f litebus-test ]; then
    echo "litebus-test do not exist!"
    exit 1
fi

exit_ret=0

function test_httpkmsg_not_enable() {
    echo "test_httpkmsg_not_enable enter..."
    # test http
    env -u LITEBUS_HTTPKMSG_ENABLED PROTOCOL=tcp ./litebus-test --gtest_filter=HTTPTest.*
    if [ $? -ne 0 ]; then
        echo "litebus-http-test on tcp failed"
        exit_ret=1
        exit $exit_ret
    fi

    #test non-http
    env -u LITEBUS_HTTPKMSG_ENABLED PROTOCOL=tcp ./litebus-test --gtest_shuffle --gtest_filter=-*HTTPTest.*
    if [ $? -ne 0 ]; then
        echo "litebus-test on tcp failed"
        exit_ret=2
        exit $exit_ret
    fi
}

function test_httpkmsg_enable() {
    echo "test_httpkmsg_enable enter..."
    # test http
    env LITEBUS_HTTPKMSG_ENABLED=1 PROTOCOL=tcp ./litebus-test --gtest_filter=HTTPTest.*
    if [ $? -ne 0 ]; then
        echo "HTTPKMSG: litebus-http-test on tcp failed"
        exit_ret=3
        exit $exit_ret
    fi

    # #test non-http
    env LITEBUS_HTTPKMSG_ENABLED=1 PROTOCOL=tcp ./litebus-test --gtest_shuffle --gtest_filter=ExecTest.*
    if [ $? -ne 0 ]; then
        echo "HTTPKMSG: litebus-test on tcp failed"
        exit_ret=4
        exit $exit_ret
    fi
}

if [ "$TEST_MODEL" == "on" ];then
    echo "test httpkmsg enable"
    test_httpkmsg_enable
elif [ "$TEST_MODEL" == "off" ];then
    echo "test httpkmsg not enable"
    test_httpkmsg_not_enable
else
    echo "Running tests in parallel subShells..."

    ( test_httpkmsg_enable ) > enable.log 2>&1 &
    PID1=$!

    ( test_httpkmsg_not_enable ) > disable.log 2>&1 &
    PID2=$!

    # wait tasks
    wait $PID1
    EXIT_ENABLE=$?

    wait $PID2
    EXIT_DISABLE=$?

    if [ $EXIT_ENABLE -ne 0 ] || [ $EXIT_DISABLE -ne 0 ]; then
        echo "Error: Some tests failed."
        if [ $EXIT_ENABLE -ne 0 ]; then
            echo "<<<<<<<<<<< test_httpkmsg_enable log"
            cat enable.log
            echo "<<<<<<<<<<< test_httpkmsg_enable log end"
        fi
        if [ $EXIT_DISABLE -ne 0 ]; then
            echo "<<<<<<<<<<< test_httpkmsg_not_enable log"
            cat disable.log
            echo "<<<<<<<<<<< test_httpkmsg_not_enable log end"
        fi
        exit 1
    else
        echo "All tests passed."
    fi
fi

# not-httpkmsg has been tested. only test httpkmsg modes in the future.
export LITEBUS_HTTPKMSG_ENABLED=1 # must do this
# test send not on remote link
export LITEBUS_SEND_ON_REMOTE="true"
echo "TCPTest.*:SSLTest. enter111111111"
PROTOCOL=tcp ./litebus-test --gtest_filter=TCPTest.*:SSLTest.*
if [ $? -ne 0 ]; then
    echo "send not on remote link: litebus-test on tcp failed"
    exit_ret=7
    exit $exit_ret
fi
unset LITEBUS_SEND_ON_REMOTE

# test http long time no msg
export LITEBUS_LINK_RECYCLE_PERIOD="20"
echo "LongTimeNoComm enter111111111"
PROTOCOL=tcp ./litebus-test --gtest_filter=HTTPTest.LongTimeNoComm
if [ $? -ne 0 ]; then
    echo "test http long time no ms on tcp failed"
    exit_ret=1
    exit $exit_ret
fi
unset LITEBUS_LINK_RECYCLE_PERIOD

rm -rf "${SSL_UTILS_PATH}"

exit $exit_ret

