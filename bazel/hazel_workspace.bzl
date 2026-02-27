load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def hw_rules():
    http_archive(
        name = "bazel_skylib",
        sha256 = "74d544d96f4a5bb630d465ca8bbcfe231e3594e5aae57e1edbf17a6eb3ca2506",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.3.0/bazel-skylib-1.3.0.tar.gz",
        ],
    )

    http_archive(
        name = "rules_cc",
        sha256 = "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf",
        strip_prefix = "rules_cc-0.0.9",
        urls = [
            "https://github.com/bazelbuild/rules_cc/releases/download/0.0.9/rules_cc-0.0.9.tar.gz",
        ],
    )

    http_archive(
        name = "rules_proto",
        sha256 = "dc3fb206a2cb3441b485eb1e423165b231235a1ea9b031b4433cf7bc1fa460dd",
        strip_prefix = "rules_proto-5.3.0-21.7",
        urls = [
            "https://github.com/bazelbuild/rules_proto/archive/refs/tags/5.3.0-21.7.tar.gz",
        ],
    )

    http_archive(
        name = "rules_python",
        urls = [
            "https://github.com/bazelbuild/rules_python/archive/refs/tags/0.19.0.tar.gz",
        ],
        sha256 = "ffc7b877c95413c82bfd5482c017edcf759a6250d8b24e82f41f3c8b8d9e287e",
        strip_prefix = "rules_python-0.19.0",
    )

    http_archive(
        name = "rules_foreign_cc",
        # Not in bazelbuild/ org — not on Huawei mirror; fetch directly from GitHub
        urls = [
            "https://github.com/bazel-contrib/rules_foreign_cc/archive/refs/tags/0.9.0.tar.gz",
        ],
        sha256 = "2a4d07cd64b0719b39a7c12218a3e507672b82a97b98c6a89d38565894cf7c51",
        strip_prefix = "rules_foreign_cc-0.9.0",
    )

    # --- rules_apple / rules_swift / apple_support ---
    # Required by gRPC's Bazel build (rules_apple is loaded by grpc_deps).
    # On Linux these are never actually invoked, but must be resolvable.
    http_archive(
        name = "build_bazel_rules_apple",
        sha256 = "d6735ed25754dbcb4fce38e6d72c55b55f6afa91408e0b72f1357640b88bb49c",
        strip_prefix = "rules_apple-0.31.3",
        urls = [
            "https://github.com/bazelbuild/rules_apple/archive/refs/tags/0.31.3.tar.gz",
        ],
    )

    http_archive(
        name = "build_bazel_rules_swift",
        sha256 = "802c094df1642909833b59a9507ed5f118209cf96d13306219461827a00992da",
        strip_prefix = "rules_swift-0.21.0",
        urls = [
            "https://github.com/bazelbuild/rules_swift/archive/refs/tags/0.21.0.tar.gz",
        ],
    )

    http_archive(
        name = "build_bazel_apple_support",
        sha256 = "c02a8c902f405e5ea12b815f426fbe429bc39a2628b290e50703d956d40f5542",
        strip_prefix = "apple_support-0.10.0",
        urls = [
            "https://github.com/bazelbuild/apple_support/archive/refs/tags/0.10.0.tar.gz",
        ],
    )
