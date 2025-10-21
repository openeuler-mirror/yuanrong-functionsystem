load("@yuanrong_multi_language_runtime//bazel:yr.bzl", "filter_files_with_suffix")

cc_library(
    name = "lib_datasystem_sdk",
    srcs = [
        "cpp/lib/libdatasystem.so",
        "cpp/lib/libsecurec.so",
        "cpp/lib/libzmq.so.5",
    ] + glob([
        "cpp/lib/libds-spdlog.so.*",
        "cpp/lib/libtbb.so*",
        "cpp/lib/libcurl.so*",
    ]),
    hdrs = glob(["cpp/include/**/*.h"]),
    strip_include_prefix = "cpp/include",
    visibility = ["//visibility:public"],
    alwayslink = True,
)

filter_files_with_suffix(
    name = "shared",
    srcs = glob(["cpp/lib/lib*.so*"]),
    suffix = ".so",
    visibility = ["//visibility:public"],
)
