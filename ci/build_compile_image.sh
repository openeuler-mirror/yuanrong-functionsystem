#!/bin/bash

BASE_DIR=$(
    cd "$(dirname "$0")"
    pwd
)
TAG=$1

cd "$BASE_DIR"

rm -rf $BASE_DIR/../vendor/output/Build
rm -rf $BASE_DIR/../vendor/output/Download
rm -rf $BASE_DIR/../vendor/output/Source
rm -rf $BASE_DIR/../vendor/output/Stamp
rm -rf $BASE_DIR/../vendor/output/tmp
rm -rf $BASE_DIR/../vendor/output/Install/datasystem

docker build -t "$TAG" -f compile.Dockerfile ../
