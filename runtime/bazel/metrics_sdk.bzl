load("@yuanrong_multi_language_runtime//bazel:yr.bzl", "filter_files_with_suffix")

cc_library(
    name = "lib_metrics_sdk",
    srcs = glob([
        "lib/libobservability-metrics.so",
        "lib/libobservability-metrics-sdk.so",
        "lib/liblitebus.so.0.0.1",
        "lib/libyrlogs.so",
        "lib/libspdlog.so.1.*",
        "lib/libspdlog.so.1.*.0",
    ]),
    hdrs = glob(["include/metrics/**/*.h"]),
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
