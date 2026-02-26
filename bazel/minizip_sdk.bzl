cc_library(
    name = "minizip_sdk",
    srcs = ["lib/libminizip.a"],
    hdrs = glob(["include/**/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)
