load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@io_bazel_rules_go//go:deps.bzl", "go_rules_dependencies")

def grpc_extra_deps():
    protobuf_deps()
    switched_rules_by_language(
        name = "com_google_googleapis_imports",
        cc = True,
        grpc = True,
    )
    # Register io_bazel_rules_go internal deps (including io_bazel_rules_go_name_hack),
    # needed because grpc_deps() loads io_bazel_rules_go for xDS/CNCF support.
    go_rules_dependencies()
