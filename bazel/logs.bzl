load("@yuanrong_functionsystem//bazel:yr.bzl", "filter_files_with_suffix")

cc_library(
    name = "logs_sdk",
    # Only expose libyrlogs.so — libspdlog.so and libz.so are bundled runtime
    # copies; spdlog is a header-only dependency for compile-time use.
    srcs = ["lib/libyrlogs.so"],
    hdrs = glob(["include/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    alwayslink = True,
)

filter_files_with_suffix(
    name = "shared",
    srcs = glob(["lib/lib*.so*"]),
    suffix = ".so",
    visibility = ["//visibility:public"],
)
