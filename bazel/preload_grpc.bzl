load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:grpc_upb_repository.bzl", "grpc_upb_repository")

def preload_grpc():
    # abseil-cpp — GitHub stable tarball (gitee /repository/archive/ zips are non-deterministic)
    # Not in github.com/bazelbuild/ so NOT redirected to Huawei mirror
    http_archive(
        name = "com_google_absl",
        sha256 = "f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3",
        strip_prefix = "abseil-cpp-20240722.0",
        urls = [
            "https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz",
        ],
    )

    # protobuf v3.25.5 — GitHub stable tarball
    http_archive(
        name = "com_google_protobuf",
        strip_prefix = "protobuf-3.25.5",
        sha256 = "4356e78744dfb2df3890282386c8568c85868116317d9b3ad80eb11c2aecf2ff",
        urls = ["https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.25.5.tar.gz"],
    )

    # utf8_range (required by protobuf)
    http_archive(
        name = "utf8_range",
        strip_prefix = "utf8_range-d863bc33e15cba6d873c878dcca9e6fe52b2f8cb",
        sha256 = "568988b5f7261ca181468dba38849fabf59dd9200fb2ed4b2823da187ef84d8c",
        urls = ["https://github.com/protocolbuffers/utf8_range/archive/d863bc33e15cba6d873c878dcca9e6fe52b2f8cb.zip"],
    )

    # zlib (required by protobuf/grpc) — GitHub stable tarball
    http_archive(
        name = "zlib",
        strip_prefix = "zlib-1.3.1",
        urls = ["https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz"],
        sha256 = "17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c",
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

    # re2 — GitHub stable tarball
    http_archive(
        name = "com_googlesource_code_re2",
        urls = ["https://github.com/google/re2/archive/refs/tags/2024-02-01.tar.gz"],
        sha256 = "cd191a311b84fcf37310e5cd876845b4bf5aee76fdd755008eef3b6478ce07bb",
        strip_prefix = "re2-2024-02-01",
    )

    # googleapis
    http_archive(
        name = "com_google_googleapis",
        url = "https://github.com/googleapis/googleapis/archive/541b1ded4abadcc38e8178680b0677f65594ea6f.zip",
        sha256 = "7ebab01b06c555f4b6514453dc3e1667f810ef91d1d4d2d3aa29bb9fcb40a900",
        strip_prefix = "googleapis-541b1ded4abadcc38e8178680b0677f65594ea6f",
    )

    # c-ares — GitHub stable tarball
    http_archive(
        name = "com_github_cares_cares",
        urls = ["https://github.com/c-ares/c-ares/archive/refs/tags/cares-1_19_1.tar.gz"],
        sha256 = "9eadec0b34015941abdf3eb6aead694c8d96a192a792131186a7e0a86f2ad6d9",
        strip_prefix = "c-ares-cares-1_19_1",
    )
