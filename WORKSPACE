workspace(name = "yuanrong_functionsystem")

load("//bazel:hazel_workspace.bzl", "hw_rules")

hw_rules()

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("//bazel:local_patched_repository.bzl", "local_patched_repository")

# --- spdlog (local, patches pre-applied) ---
local_patched_repository(
    name = "spdlog",
    path = "./vendor/src/spdlog/",
    build_file = "@//bazel:spdlog.bzl",
)

# --- nlohmann_json --- gitee mirror zip (GitHub .tar.gz returns 404 on Huawei mirror;
# the .zip is pre-downloaded in thirdparty/runtime_deps/v3.11.3.zip)
http_archive(
    name = "nlohmann_json",
    build_file = "@//bazel:nlohmann_json.bzl",
    sha256 = "0deac294b2c96c593d0b7c0fb2385a2f4594e8053a36c52b11445ef4b9defebb",
    strip_prefix = "nlohmann-json-v3.11.3",
    urls = ["https://gitee.com/mirrors/nlohmann-json/repository/archive/v3.11.3.zip"],
)

# --- gtest --- zip archive (the .tar.gz variant returns 404 on Huawei mirror;
# the .zip is pre-downloaded in thirdparty/runtime_deps/v1.13.0.zip)
http_archive(
    name = "gtest",
    sha256 = "647924848ca7cb91ba5e34260132902886e1bd140428bd3bd7b4e8fa6c6c8904",
    strip_prefix = "googletest-v1.13.0",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.13.0.zip"],
)

# --- grpc dependency chain (abseil, protobuf, zlib, grpc, boringssl, re2, etc.) ---
load("//bazel:preload_grpc.bzl", "preload_grpc")

preload_grpc()

# --- opentelemetry pre-loading (must come after grpc preload) ---
load("//bazel:preload_opentelemetry.bzl", "preload_opentelemetry")

preload_opentelemetry()

# --- opentelemetry-cpp (must come after preload_opentelemetry) ---
http_archive(
    name = "opentelemetry_cpp",
    sha256 = "7735cc56507149686e6019e06f588317099d4522480be5f38a2a09ec69af1706",
    strip_prefix = "opentelemetry-cpp-1.13.0",
    urls = ["https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.13.0.tar.gz"],
)

load("@opentelemetry_cpp//bazel:repository.bzl", "opentelemetry_cpp_deps")

opentelemetry_cpp_deps()

load("@opentelemetry_cpp//bazel:extra_deps.bzl", "opentelemetry_extra_deps")

opentelemetry_extra_deps()

# --- grpc deps and extra deps (must come after otel) ---
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

# --- yaml-cpp --- zip archive (the .tar.gz variant returns 404 on Huawei mirror;
# the .zip is pre-downloaded in thirdparty/runtime_deps/0.8.0.zip)
http_archive(
    name = "yaml-cpp",
    sha256 = "6a05c681872d9465b8e2040b5211b1aa5cf30151dc4f3d7ed23ac75ce0fd9944",
    strip_prefix = "yaml-cpp-0.8.0",
    urls = ["https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.zip"],
)

# --- securec (libboundscheck) ---
maybe(
    new_local_repository,
    name = "securec",
    build_file = "@//bazel:securec.bzl",
    path = "./vendor/src/libboundscheck",
)

# --- Pre-built common libraries ---
new_local_repository(
    name = "litebus",
    build_file = "@//bazel:litebus.bzl",
    path = "./common/litebus/output/",
)

new_local_repository(
    name = "logs_sdk",
    build_file = "@//bazel:logs.bzl",
    path = "./common/logs/output/",
)

new_local_repository(
    name = "metrics_sdk",
    build_file = "@//bazel:metrics_sdk.bzl",
    path = "./common/metrics/output/",
)

new_local_repository(
    name = "datasystem_sdk",
    build_file = "@//bazel:datasystem_sdk.bzl",
    path = "./vendor/src/datasystem/sdk/cpp/",
)

# --- Pre-built vendor libraries (built by run.sh vendor step) ---
new_local_repository(
    name = "etcdapi",
    build_file = "@//bazel:etcdapi.bzl",
    path = "./vendor/output/Install/etcdapi/",
)

new_local_repository(
    name = "obs_sdk",
    build_file = "@//bazel:obs_sdk.bzl",
    path = "./vendor/output/Install/obs/",
)

new_local_repository(
    name = "curl_sdk",
    build_file = "@//bazel:curl_sdk.bzl",
    path = "./vendor/output/Install/curl/",
)

new_local_repository(
    name = "opentelemetry_prebuilt",
    build_file = "@//bazel:opentelemetry_prebuilt.bzl",
    path = "./vendor/output/Install/opentelemetry/",
)

new_local_repository(
    name = "minizip_sdk",
    build_file = "@//bazel:minizip_sdk.bzl",
    path = "./vendor/output/Install/zlib/",
)
