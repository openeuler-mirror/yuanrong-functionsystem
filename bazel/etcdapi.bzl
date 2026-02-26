# Build file for pre-built etcdapi (etcd client proto library)
# Installed at vendor/output/Install/etcdapi/

cc_library(
    name = "etcdapi",
    hdrs = glob(["include/**/*.h"]),
    srcs = ["lib/libetcdapi_proto.a"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
