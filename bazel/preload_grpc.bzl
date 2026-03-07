load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:grpc_upb_repository.bzl", "grpc_upb_repository")

def preload_grpc():
    # abseil-cpp — zip archive (the .tar.gz variant returns 404 on Huawei mirror;
    # the .zip is pre-downloaded in thirdparty/runtime_deps/20240722.0.zip)
    http_archive(
        name = "com_google_absl",
        sha256 = "104dead3edd7b67ddeb70c37578245130d6118efad5dad4b618d7e26a5331f55",
        strip_prefix = "abseil-cpp-20240722.0",
        urls = [
            "https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.zip",
        ],
    )

    # protobuf v3.25.5 — zip archive (the .tar.gz variant returns 404 on Huawei mirror;
    # the .zip is pre-downloaded in thirdparty/runtime_deps/v3.25.5.zip)
    http_archive(
        name = "com_google_protobuf",
        strip_prefix = "protobuf_source-v3.25.5",
        sha256 = "4640cb69abb679e2a4b061dfeb7debb3170b592e4ac6e3f16dbaaa4aac0710bd",
        urls = ["https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.25.5.zip"],
    )

    # utf8_range (required by protobuf)
    http_archive(
        name = "utf8_range",
        strip_prefix = "utf8_range-d863bc33e15cba6d873c878dcca9e6fe52b2f8cb",
        sha256 = "568988b5f7261ca181468dba38849fabf59dd9200fb2ed4b2823da187ef84d8c",
        urls = ["https://github.com/protocolbuffers/utf8_range/archive/d863bc33e15cba6d873c878dcca9e6fe52b2f8cb.zip"],
    )

    # zlib (required by protobuf/grpc) — zip archive (the .tar.gz variant returns 404 on Huawei mirror;
    # the .zip is pre-downloaded in thirdparty/runtime_deps/v1.3.1.zip)
    http_archive(
        name = "zlib",
        strip_prefix = "zlib-v1.3.1",
        urls = ["https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip"],
        sha256 = "7c31009abc4e76ddc32e1448b6051bafe5f606aac158bb36166100a21ec170c6",
        build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
    )

    # grpc — local vendor source; use local_repository since gRPC ships its own BUILD/WORKSPACE files
    native.local_repository(
        name = "com_github_grpc_grpc",
        path = "./vendor/src/grpc",
    )

    # upb — extracted from grpc third_party
    grpc_upb_repository(
        name = "upb",
        path = Label("@com_github_grpc_grpc//:WORKSPACE"),
    )

    # boringssl/openssl — local vendor source
    native.new_local_repository(
        name = "boringssl",
        build_file = "//bazel:openssl.bazel",
        path = "./vendor/src/openssl/",
    )

    # re2 — zip archive (the .tar.gz variant returns 404 on Huawei mirror;
    # the .zip is pre-downloaded in thirdparty/runtime_deps/2024-02-01.zip)
    http_archive(
        name = "com_googlesource_code_re2",
        urls = ["https://github.com/google/re2/archive/refs/tags/2024-02-01.zip"],
        sha256 = "54bff0e995b101e1865dcea5d052ec10b3aadb6f8c57b5c03c9eeccddb00a08a",
        strip_prefix = "re2-2024-02-01",
    )

    # googleapis
    http_archive(
        name = "com_google_googleapis",
        url = "https://github.com/googleapis/googleapis/archive/541b1ded4abadcc38e8178680b0677f65594ea6f.zip",
        sha256 = "7ebab01b06c555f4b6514453dc3e1667f810ef91d1d4d2d3aa29bb9fcb40a900",
        strip_prefix = "googleapis-541b1ded4abadcc38e8178680b0677f65594ea6f",
    )

    # c-ares — zip archive (the .tar.gz variant returns 404 on Huawei mirror;
    # the .zip is pre-downloaded in thirdparty/runtime_deps/cares-1_19_1.zip)
    http_archive(
        name = "com_github_cares_cares",
        urls = ["https://github.com/c-ares/c-ares/archive/refs/tags/cares-1_19_1.zip"],
        sha256 = "edcaac184aff0e6b6eb7b9ede7a55f36c7fc04085d67fecff2434779155dd8ce",
        strip_prefix = "c-ares-cares-1_19_1",
    )
