# Dockerfile for compile with cache

FROM swr.cn-southwest-2.myhuaweicloud.com/yuanrong-dev/compile_x86:2.1

COPY ./vendor/output /cache/output/

RUN mkdir -p /cache/src/ && \
    mkdir /cache/src/abseil-cpp && \
    mkdir /cache/src/huaweicloud-sdk-c-obs && \
    mkdir /cache/src/libboundscheck && \
    mkdir /cache/src/yaml-cpp