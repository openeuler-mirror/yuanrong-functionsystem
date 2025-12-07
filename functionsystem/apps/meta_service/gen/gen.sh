#!/bin/bash

set -e
BASE_DIR=$(cd "$(dirname "$0")"; pwd)
PROJECT_DIR="${BASE_DIR}"/../..
DIR_META_SERVICE=${PROJECT_DIR}/meta_service/function_repo/storage

go run ${BASE_DIR}/gen.go -- ${DIR_META_SERVICE}/types.go ${DIR_META_SERVICE}/kv.go