package(default_visibility = ["//visibility:public"])

cc_library(
    name = "grpc",
    hdrs = glob(["include/grpc/**/*.h"]),
    includes = ["include"],
    deps = ["@grpc_runtime_libs//:grpc_shared"],
)

cc_library(
    name = "gpr",
    hdrs = glob(["include/grpc/support/**/*.h"]),
    includes = ["include"],
    deps = ["@grpc_runtime_libs//:gpr_shared"],
)

cc_library(
    name = "grpcpp",
    hdrs = glob(["include/grpcpp/**/*.h"]),
    includes = ["include"],
    deps = [
        ":grpc",
        "@grpc_runtime_libs//:grpcpp_shared",
    ],
)
