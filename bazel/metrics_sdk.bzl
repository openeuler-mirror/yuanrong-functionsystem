load("@yuanrong_functionsystem//bazel:yr.bzl", "filter_files_with_suffix")

cc_library(
    name = "metrics_sdk",
    # Expose only the observability-metrics-specific libraries.
    # The bundled libopentelemetry_*.so are provided by @opentelemetry_prebuilt,
    # and the bundled litebus/ssl/crypto/yrlogs/spdlog/curl are provided by their
    # respective packages. Only the unique observability-metrics libs go here.
    srcs = glob([
        "lib/libobservability-metrics.so",
        "lib/libobservability-metrics-sdk.so",
        "lib/libobservability-metrics-exporter-ostream.so",
        "lib/libobservability-metrics-file-exporter.so",
        "lib/libobservability-metrics-opentelemetry-exporter.so",
        "lib/libobservability-prometheus-push-exporter.so",
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
