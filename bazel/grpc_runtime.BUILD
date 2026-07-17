package(default_visibility = ["//visibility:public"])

cc_import(
    name = "grpc_shared",
    shared_library = "lib/libgrpc.so",
)

cc_import(
    name = "gpr_shared",
    shared_library = "lib/libgpr.so",
)

cc_import(
    name = "grpcpp_shared",
    shared_library = "lib/libgrpc++.so",
)

cc_library(
    name = "grpc",
    hdrs = glob(["include/grpc/**/*.h"]),
    includes = ["include"],
    deps = [":grpc_shared"],
)

cc_library(
    name = "gpr",
    hdrs = glob(["include/grpc/support/**/*.h"]),
    includes = ["include"],
    deps = [":gpr_shared"],
)

cc_library(
    name = "grpcpp",
    hdrs = glob(["include/grpcpp/**/*.h"]),
    includes = ["include"],
    deps = [
        ":grpc",
        ":grpcpp_shared",
    ],
)
