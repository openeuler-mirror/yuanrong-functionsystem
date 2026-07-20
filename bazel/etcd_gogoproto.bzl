load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "gogoproto_proto",
    srcs = ["gogoproto/gogo.proto"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
    visibility = ["//visibility:public"],
)
