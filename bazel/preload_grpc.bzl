load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:grpc_upb_repository.bzl", "grpc_upb_repository")

def preload_grpc():
    # Use one shared gRPC/GPR implementation when DataSystem is loaded into
    # FunctionSystem processes; static duplicates register gflags twice.
    native.new_local_repository(
        name = "grpc_runtime_libs",
        build_file = "//bazel:grpc_runtime_libs.BUILD",
        path = "./vendor/src/datasystem/sdk/cpp/lib",
    )

    native.new_local_repository(
        name = "grpc_runtime",
        build_file = "//bazel:grpc_runtime.BUILD",
        path = "./vendor/src/grpc",
    )

    # abseil-cpp — gitee.com mirror zip
    http_archive(
        name = "com_google_absl",
        sha256 = "104dead3edd7b67ddeb70c37578245130d6118efad5dad4b618d7e26a5331f55",
        strip_prefix = "abseil-cpp-20240722.0",
        urls = [
            "https://gitee.com/mirrors/abseil-cpp/repository/archive/20240722.0.zip",
        ],
    )

    # protobuf v3.25.5 — the vendor step already extracts a Bazel-ready source
    # tree. Reuse it instead of fetching the same sources from GitHub again.
    native.local_repository(
        name = "com_google_protobuf",
        path = "./vendor/src/protobuf",
    )

    # utf8_range (required by protobuf)
    http_archive(
        name = "utf8_range",
        strip_prefix = "utf8_range-d863bc33e15cba6d873c878dcca9e6fe52b2f8cb",
        sha256 = "568988b5f7261ca181468dba38849fabf59dd9200fb2ed4b2823da187ef84d8c",
        urls = ["https://github.com/protocolbuffers/utf8_range/archive/d863bc33e15cba6d873c878dcca9e6fe52b2f8cb.zip"],
    )

    # zlib (required by protobuf/grpc) — gitee.com mirror zip
    http_archive(
        name = "zlib",
        strip_prefix = "zlib-v1.3.1",
        urls = ["https://gitee.com/mirrors/zlib/repository/archive/v1.3.1.zip"],
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

    # boringssl/openssl — cache-managed vendor installation
    native.new_local_repository(
        name = "boringssl",
        build_file = "//bazel:openssl.bazel",
        path = "./vendor/output/Install/openssl/",
    )

    # re2 — gitee.com mirror zip
    http_archive(
        name = "com_googlesource_code_re2",
        url = "https://gitee.com/mirrors/re2/repository/archive/2024-02-01.zip",
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

    # c-ares — gitee.com mirror zip
    # build_file required: gitee.com archive does not include Bazel BUILD; use gRPC's template
    http_archive(
        name = "com_github_cares_cares",
        url = "https://gitee.com/mirrors/c-ares/repository/archive/cares-1_19_1.zip",
        sha256 = "edcaac184aff0e6b6eb7b9ede7a55f36c7fc04085d67fecff2434779155dd8ce",
        strip_prefix = "c-ares-cares-1_19_1",
        build_file = "@com_github_grpc_grpc//third_party:cares/cares.BUILD",
    )
