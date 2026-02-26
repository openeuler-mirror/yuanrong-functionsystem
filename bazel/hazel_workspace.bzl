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
