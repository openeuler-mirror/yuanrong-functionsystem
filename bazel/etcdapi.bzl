# Build file for etcdapi — Bazel source build of etcd C++ client proto/gRPC library.
# Used as build_file for new_local_repository(name="etcdapi") in WORKSPACE.
# Replaces the old CMake prebuilt (vendor/output/Install/etcdapi/).
# All etcd proto/gRPC targets are built from source under //vendor/src/etcd/.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "etcdapi",
    visibility = ["//visibility:public"],
    deps = [
        "@yuanrong_functionsystem//vendor/src/etcd/api/authpb:authpb_cc_proto",
        "@yuanrong_functionsystem//vendor/src/etcd/api/etcdserverpb:etcdserverpb_cc_grpc",
        "@yuanrong_functionsystem//vendor/src/etcd/api/etcdserverpb:etcdserverpb_cc_proto",
        "@yuanrong_functionsystem//vendor/src/etcd/api/mvccpb:mvccpb_cc_proto",
        "@yuanrong_functionsystem//vendor/src/etcd/server/etcdserver/api/v3election/v3electionpb:v3electionpb_cc_grpc",
        "@yuanrong_functionsystem//vendor/src/etcd/server/etcdserver/api/v3election/v3electionpb:v3electionpb_cc_proto",
        "@yuanrong_functionsystem//vendor/src/etcd/server/etcdserver/api/v3lock/v3lockpb:v3lockpb_cc_grpc",
        "@yuanrong_functionsystem//vendor/src/etcd/server/etcdserver/api/v3lock/v3lockpb:v3lockpb_cc_proto",
    ],
)
