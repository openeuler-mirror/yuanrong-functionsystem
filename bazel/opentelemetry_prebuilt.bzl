# Build file for pre-built OpenTelemetry (vendor/output/Install/opentelemetry/)
# Used by trace_adapter for trace exporting

cc_library(
    name = "opentelemetry_prebuilt",
    hdrs = glob(["include/**/*.h", "include/**/*.hpp"]),
    srcs = glob(["lib/libopentelemetry*.so"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
