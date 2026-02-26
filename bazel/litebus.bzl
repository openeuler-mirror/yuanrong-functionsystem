load("@yuanrong_functionsystem//bazel:yr.bzl", "filter_files_with_suffix")

cc_library(
    name = "litebus",
    # Only expose liblitebus.so — the bundled ssl/crypto/spdlog/yrlogs/securec
    # inside the litebus output are runtime copies; the functionsystem binary links
    # those separately via openssl, logs_sdk, and securec packages.
    srcs = ["lib/liblitebus.so"],
    hdrs = glob(["include/**/*.hpp", "include/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    alwayslink = True,
    deps = ["@securec//:securec"],
)

filter_files_with_suffix(
    name = "shared",
    srcs = glob(["lib/lib*.so*"]),
    suffix = ".so",
    visibility = ["//visibility:public"],
)
