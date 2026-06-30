# Build file for pre-built Huawei OBS SDK
# Installed at vendor/output/Install/obs/
# Note: only top-level headers are included (no platform-specific subdirs)

cc_library(
    name = "obs_sdk",
    hdrs = glob(["include/**/*.h", "include/*.h"]),
    srcs = [
        "lib/libeSDKOBS.so",
        "lib/libeSDKLogAPI.so",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
