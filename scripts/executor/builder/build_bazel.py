# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
"""Bazel build orchestration for functionsystem C++ binaries.

Workflow:
  1. Check that 'bazel' is available in PATH.
  2. Copy proto files into common/proto/posix/ (same as CMake path).
  3. Run `bazel build //functionsystem/src/...` to build all binaries.
  4. Copy Bazel output binaries to functionsystem/output/bin/ (mirrors cmake install).
"""

import os
import shutil

import utils

log = utils.stream_logger()

# Bazel targets that correspond to production binaries
BINARY_TARGETS = [
    "//functionsystem/src/function_proxy:function_proxy",
    "//functionsystem/src/function_master:function_master",
    "//functionsystem/src/function_agent:function_agent",
    "//functionsystem/src/domain_scheduler:domain_scheduler",
    "//functionsystem/src/runtime_manager:runtime_manager",
    "//functionsystem/src/iam_server:iam_server",
]


def check_bazel_available():
    """Raise RuntimeError if bazel is not found in PATH."""
    bazel_path = shutil.which("bazel")
    if bazel_path is None:
        raise RuntimeError(
            "Bazel toolchain not found in PATH. "
            "Please ensure Bazel 6.x is installed in the compile container before building with --builder bazel."
        )
    log.info(f"Found bazel at: {bazel_path}")
    return bazel_path


def build_binary_bazel(root_dir: str, job_num: int, version: str, build_type: str = "Release"):
    """Build all functionsystem C++ binaries using Bazel and copy artifacts to output/.

    Args:
        root_dir: Workspace root (yuanrong-functionsystem/)
        job_num: Parallel job count passed to --jobs
        version: Build version string (e.g. "1.0.0")
        build_type: "Release" or "Debug"
    """
    check_bazel_available()

    # Determine bazel config flag
    config = "release" if build_type.lower() == "release" else "debug"

    output_dir = os.path.join(root_dir, "functionsystem", "output")
    bin_output_dir = os.path.join(output_dir, "bin")
    os.makedirs(bin_output_dir, exist_ok=True)

    log.info(f"Running Bazel build with config={config}, jobs={job_num}")

    # Build all binary targets
    bazel_cmd = [
        "bazel", "build",
        f"--jobs={job_num}",
        f"--config={config}",
        *BINARY_TARGETS,
    ]
    utils.sync_command(bazel_cmd, cwd=root_dir)

    # Copy Bazel-built binaries to functionsystem/output/bin/
    _copy_bazel_outputs(root_dir, bin_output_dir)

    # Copy required shared libraries to functionsystem/output/lib/
    _copy_shared_libraries(root_dir, output_dir)

    log.info(f"Bazel build complete. Binaries installed to {bin_output_dir}")


def _copy_bazel_outputs(root_dir: str, bin_output_dir: str):
    """Copy compiled binaries from bazel-bin/ into functionsystem/output/bin/."""
    binary_names = [
        "function_proxy",
        "function_master",
        "function_agent",
        "domain_scheduler",
        "runtime_manager",
        "iam_server",
    ]
    bazel_bin = os.path.join(root_dir, "bazel-bin")
    src_dirs = [
        os.path.join(bazel_bin, "functionsystem", "src", "function_proxy"),
        os.path.join(bazel_bin, "functionsystem", "src", "function_master"),
        os.path.join(bazel_bin, "functionsystem", "src", "function_agent"),
        os.path.join(bazel_bin, "functionsystem", "src", "domain_scheduler"),
        os.path.join(bazel_bin, "functionsystem", "src", "runtime_manager"),
        os.path.join(bazel_bin, "functionsystem", "src", "iam_server"),
    ]
    for binary, src_dir in zip(binary_names, src_dirs):
        src_binary = os.path.join(src_dir, binary)
        dst_binary = os.path.join(bin_output_dir, binary)
        if os.path.isfile(src_binary):
            shutil.copy2(src_binary, dst_binary)
            log.info(f"Installed {binary} -> {dst_binary}")
        else:
            log.warning(f"Binary not found in bazel-bin: {src_binary}")


def _copy_shared_libraries(root_dir: str, output_dir: str):
    """Copy required shared libraries to output/lib/.

    Only pre-built (.so) dependencies are copied.  Bazel-managed deps
    (gRPC, protobuf, abseil, boringssl, c-ares) are statically linked into
    the binaries and do not need runtime .so files.

    Source directories mirror the new_local_repository declarations in WORKSPACE:
      @litebus              → common/litebus/output/lib/
      @logs_sdk             → common/logs/output/lib/
      @metrics_sdk          → common/metrics/output/lib/
      @datasystem_sdk       → vendor/src/datasystem/sdk/cpp/lib/
      @obs_sdk              → vendor/output/Install/obs/lib/
      @curl_sdk             → vendor/output/Install/curl/lib/
      @opentelemetry_prebuilt → vendor/output/Install/opentelemetry/lib/
    Note: @etcdapi and @minizip_sdk use static (.a) libs only — skipped.
    """
    lib_output_dir = os.path.join(output_dir, "lib")
    os.makedirs(lib_output_dir, exist_ok=True)

    vendor_install = os.path.join(root_dir, "vendor", "output", "Install")

    lib_search_dirs = [
        # Pre-built common libraries
        os.path.join(root_dir, "common", "litebus", "output", "lib"),   # @litebus
        os.path.join(root_dir, "common", "logs", "output", "lib"),      # @logs_sdk
        os.path.join(root_dir, "common", "metrics", "output", "lib"),   # @metrics_sdk
        # Pre-built vendor libraries
        os.path.join(root_dir, "vendor", "src", "datasystem", "sdk", "cpp", "lib"),  # @datasystem_sdk
        os.path.join(vendor_install, "obs", "lib"),             # @obs_sdk
        os.path.join(vendor_install, "curl", "lib"),            # @curl_sdk
        os.path.join(vendor_install, "opentelemetry", "lib"),   # @opentelemetry_prebuilt
    ]

    import glob as glob_module
    copied_count = 0

    for src_dir in lib_search_dirs:
        if not os.path.isdir(src_dir):
            log.debug(f"Shared-lib source dir not found, skipping: {src_dir}")
            continue
        for so_file in glob_module.glob(os.path.join(src_dir, "lib*.so*")):
            basename = os.path.basename(so_file)
            dst_path = os.path.join(lib_output_dir, basename)
            shutil.copy2(so_file, dst_path)
            log.info(f"Installed {basename} -> {dst_path}")
            copied_count += 1

    if copied_count > 0:
        log.info(f"Installed {copied_count} shared libraries to {lib_output_dir}")
    else:
        log.warning("No shared libraries were copied - source directories may not exist")
