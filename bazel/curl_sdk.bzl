# Build file for pre-built curl
# Installed at vendor/output/Install/curl/

cc_library(
    name = "curl",
    hdrs = glob(["include/**/*.h"]),
    srcs = glob(["lib/libcurl.so*"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
