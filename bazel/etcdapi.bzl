load("@rules_proto//proto:defs.bzl", "proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

# This BUILD file is attached to vendor/src by the @etcdapi repository rule.
# Keeping the natural "etcd/..." source paths avoids virtual-import paths that
# grpc's codegen macro cannot resolve correctly for an external repository.

proto_library(
    name = "authpb_proto",
    srcs = ["etcd/api/authpb/auth.proto"],
    deps = ["@etcd_gogoproto//:gogoproto_proto"],
)

cc_proto_library(
    name = "authpb_cc_proto",
    deps = [":authpb_proto"],
)

proto_library(
    name = "mvccpb_proto",
    srcs = ["etcd/api/mvccpb/kv.proto"],
    deps = ["@etcd_gogoproto//:gogoproto_proto"],
)

cc_proto_library(
    name = "mvccpb_cc_proto",
    deps = [":mvccpb_proto"],
)

proto_library(
    name = "etcdserverpb_proto",
    srcs = ["etcd/api/etcdserverpb/rpc.proto"],
    deps = [
        ":authpb_proto",
        ":mvccpb_proto",
        "@etcd_gogoproto//:gogoproto_proto",
        "@etcd_googleapis//:annotations_proto",
    ],
)

cc_proto_library(
    name = "etcdserverpb_cc_proto",
    deps = [":etcdserverpb_proto"],
)

cc_grpc_library(
    name = "etcdserverpb_cc_grpc",
    srcs = [":etcdserverpb_proto"],
    grpc_only = True,
    deps = [":etcdserverpb_cc_proto"],
)

proto_library(
    name = "v3electionpb_proto",
    srcs = ["etcd/server/etcdserver/api/v3election/v3electionpb/v3election.proto"],
    deps = [
        ":etcdserverpb_proto",
        ":mvccpb_proto",
        "@etcd_gogoproto//:gogoproto_proto",
        "@etcd_googleapis//:annotations_proto",
    ],
)

cc_proto_library(
    name = "v3electionpb_cc_proto",
    deps = [":v3electionpb_proto"],
)

cc_grpc_library(
    name = "v3electionpb_cc_grpc",
    srcs = [":v3electionpb_proto"],
    grpc_only = True,
    deps = [":v3electionpb_cc_proto"],
)

proto_library(
    name = "v3lockpb_proto",
    srcs = ["etcd/server/etcdserver/api/v3lock/v3lockpb/v3lock.proto"],
    deps = [
        ":etcdserverpb_proto",
        "@etcd_gogoproto//:gogoproto_proto",
        "@etcd_googleapis//:annotations_proto",
    ],
)

cc_proto_library(
    name = "v3lockpb_cc_proto",
    deps = [":v3lockpb_proto"],
)

cc_grpc_library(
    name = "v3lockpb_cc_grpc",
    srcs = [":v3lockpb_proto"],
    grpc_only = True,
    deps = [":v3lockpb_cc_proto"],
)

cc_library(
    name = "etcdapi",
    visibility = ["//visibility:public"],
    deps = [
        ":authpb_cc_proto",
        ":etcdserverpb_cc_grpc",
        ":etcdserverpb_cc_proto",
        ":mvccpb_cc_proto",
        ":v3electionpb_cc_grpc",
        ":v3electionpb_cc_proto",
        ":v3lockpb_cc_grpc",
        ":v3lockpb_cc_proto",
    ],
)
