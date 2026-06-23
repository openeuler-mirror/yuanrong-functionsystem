load("@yuanrong_functionsystem//bazel:yr.bzl", "filter_files_with_suffix")

cc_library(
    name = "datasystem_sdk",
    # Only expose the datasystem-specific libraries. The bundled copies of
    # libcurl, libssl, libcrypto, libgrpc*, libsecurec, libtbb, libzmq inside this
    # package are runtime copies linked by datasystem.so itself.
    srcs = [
        "lib/libdatasystem.so",
        "lib/libds_router_client.so",
        "lib/libetcdapi_proto.so",
        "lib/librpc_option_protos.so",
        "lib/libprotobuf.so.25.5.0",
        "lib/libabseil_dll.so.2407.0.0",
        "lib/libcommon_flags.so",
    ],
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
