load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "http_proto",
    srcs = ["google/api/http.proto"],
)

proto_library(
    name = "annotations_proto",
    srcs = ["google/api/annotations.proto"],
    deps = [
        ":http_proto",
        "@com_google_protobuf//:descriptor_proto",
    ],
    visibility = ["//visibility:public"],
)
