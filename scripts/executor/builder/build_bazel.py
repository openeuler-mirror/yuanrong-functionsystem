# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
"""Bazel build orchestration for functionsystem C++ binaries.

Workflow:
  1. Check that 'bazel' is available in PATH.
  2. Generate proto/grpc C++ sources into common/proto/pb/posix/ (same include layout as CMake).
  3. Run `bazel build //functionsystem/src/...` to build all binaries.
  4. Copy Bazel output binaries to functionsystem/output/bin/ (mirrors cmake install).
"""

import os
import shutil
from glob import glob

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

PROTO_FILES = [
    "common.proto",
    "core_service.proto",
    "runtime_rpc.proto",
    "runtime_service.proto",
    "affinity.proto",
    "inner_service.proto",
    "bus_service.proto",
    "message.proto",
    "resource.proto",
    "bus_adapter.proto",
    "runtime_launcher_interface.proto",
    "exec_service.proto",
]

GRPC_PROTO_FILES = [
    "runtime_rpc.proto",
    "inner_service.proto",
    "bus_service.proto",
    "runtime_launcher_interface.proto",
    "exec_service.proto",
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


def ensure_bazel_deps(root_dir: str):
    """Download Bazel dependency archives and populate repository_cache if needed.

    Runs tools/download_bazel_deps.sh which:
      - Downloads missing archives to thirdparty/runtime_deps/
      - Verifies sha256 checksums
      - Populates Bazel repository_cache for fully offline builds
    """
    script = os.path.join(root_dir, "tools", "download_bazel_deps.sh")
    if not os.path.isfile(script):
        log.warning(f"download_bazel_deps.sh not found at {script}, skipping.")
        return
    log.info("Ensuring Bazel dependency archives are present...")
    utils.sync_command(["bash", script], cwd=root_dir)


def generate_proto_sources(root_dir: str):
    """Generate protobuf/grpc C++ sources into functionsystem/src/common/proto/pb/posix/.

    This keeps Bazel aligned with the legacy CMake include layout:
      common/proto/pb/posix/*.pb.h
      common/proto/pb/posix/*.grpc.pb.h
    """
    protoc = _find_protoc(root_dir)
    grpc_cpp_plugin = _find_grpc_cpp_plugin(root_dir)
    if protoc is None:
        raise RuntimeError("protoc not found in PATH. Please source buildtools.sh before Bazel build.")
    if grpc_cpp_plugin is None:
        raise RuntimeError("grpc_cpp_plugin not found. Please build vendor/grpc before Bazel build.")

    proto_root = os.path.join(root_dir, "proto", "posix")
    output_dir = os.path.join(root_dir, "functionsystem", "src", "common", "proto", "pb", "posix")
    os.makedirs(output_dir, exist_ok=True)
    plugin_env = _build_grpc_plugin_env(root_dir)

    expected_outputs = _expected_generated_files(output_dir)
    if _proto_outputs_up_to_date(proto_root, expected_outputs, protoc, grpc_cpp_plugin):
        log.info("Proto sources are up to date, skipping regeneration.")
        return

    stale_outputs = _find_stale_generated_files(output_dir, expected_outputs)
    for path in sorted(expected_outputs | stale_outputs):
        if os.path.exists(path):
            os.remove(path)

    log.info(f"Generating proto sources into {output_dir}")

    cpp_cmd = [
        protoc,
        f"-I{proto_root}",
        f"--cpp_out={output_dir}",
        *PROTO_FILES,
    ]
    utils.sync_command(cpp_cmd, cwd=proto_root, env=plugin_env)

    grpc_cmd = [
        protoc,
        f"-I{proto_root}",
        f"--grpc_out={output_dir}",
        f"--plugin=protoc-gen-grpc={grpc_cpp_plugin}",
        *GRPC_PROTO_FILES,
    ]
    utils.sync_command(grpc_cmd, cwd=proto_root, env=plugin_env)


def _expected_generated_files(output_dir: str):
    outputs = set()
    for proto_file in PROTO_FILES:
        base_name, _ = os.path.splitext(proto_file)
        outputs.add(os.path.join(output_dir, f"{base_name}.pb.h"))
        outputs.add(os.path.join(output_dir, f"{base_name}.pb.cc"))
    for proto_file in GRPC_PROTO_FILES:
        base_name, _ = os.path.splitext(proto_file)
        outputs.add(os.path.join(output_dir, f"{base_name}.grpc.pb.h"))
        outputs.add(os.path.join(output_dir, f"{base_name}.grpc.pb.cc"))
    return outputs


def _find_stale_generated_files(output_dir: str, expected_outputs):
    existing_outputs = set()
    for pattern in ("*.pb.h", "*.pb.cc", "*.grpc.pb.h", "*.grpc.pb.cc"):
        existing_outputs.update(glob(os.path.join(output_dir, pattern)))
    return existing_outputs - expected_outputs


def _proto_outputs_up_to_date(proto_root: str, expected_outputs, protoc: str, grpc_cpp_plugin: str):
    if not expected_outputs:
        return True

    missing_outputs = [path for path in expected_outputs if not os.path.isfile(path)]
    if missing_outputs:
        log.info(f"Proto outputs missing, regeneration required: {missing_outputs[0]}")
        return False

    stale_outputs = _find_stale_generated_files(os.path.dirname(next(iter(expected_outputs))), expected_outputs)
    if stale_outputs:
        log.info(f"Found stale generated proto outputs, regeneration required: {sorted(stale_outputs)[0]}")
        return False

    input_paths = [os.path.join(proto_root, proto_file) for proto_file in PROTO_FILES]
    input_paths.extend([protoc, grpc_cpp_plugin])
    latest_input_mtime = max(os.path.getmtime(path) for path in input_paths)
    oldest_output_mtime = min(os.path.getmtime(path) for path in expected_outputs)
    return oldest_output_mtime >= latest_input_mtime


def _find_grpc_cpp_plugin(root_dir: str):
    plugin = shutil.which("grpc_cpp_plugin")
    if plugin is not None:
        return plugin

    candidates = [
        os.path.join(root_dir, "vendor", "output", "Install", "grpc", "bin", "grpc_cpp_plugin"),
        os.path.join(root_dir, "vendor", "output", "openEuler", "Install", "grpc", "bin", "grpc_cpp_plugin"),
        os.path.join(root_dir, "vendor", "output", "Build", "grpc", "grpc_cpp_plugin"),
        os.path.join(root_dir, "vendor", "output", "openEuler", "Build", "grpc", "grpc_cpp_plugin"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def _find_protoc(root_dir: str):
    candidates = [
        os.path.join(root_dir, "vendor", "output", "Install", "protobuf", "bin", "protoc"),
        os.path.join(root_dir, "vendor", "output", "openEuler", "Install", "protobuf", "bin", "protoc"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return shutil.which("protoc")


def _build_grpc_plugin_env(root_dir: str):
    env = os.environ.copy()
    ld_library_path = env.get("LD_LIBRARY_PATH", "")
    lib_dirs = [
        os.path.join(root_dir, "vendor", "output", "Install", "grpc", "lib"),
        os.path.join(root_dir, "vendor", "output", "openEuler", "Install", "grpc", "lib"),
        os.path.join(root_dir, "vendor", "output", "Build", "grpc"),
        os.path.join(root_dir, "vendor", "output", "openEuler", "Build", "grpc"),
    ]
    existing = [path for path in lib_dirs if os.path.isdir(path)]
    if existing:
        env["LD_LIBRARY_PATH"] = ":".join(existing + ([ld_library_path] if ld_library_path else []))
    return env


def build_binary_bazel(root_dir: str, job_num: int, version: str, build_type: str = "Release"):
    """Build all functionsystem C++ binaries using Bazel and copy artifacts to output/.

    Args:
        root_dir: Workspace root (yuanrong-functionsystem/)
        job_num: Parallel job count passed to --jobs
        version: Build version string (e.g. "1.0.0")
        build_type: "Release" or "Debug"
    """
    check_bazel_available()
    ensure_bazel_deps(root_dir)
    generate_proto_sources(root_dir)

    # Determine bazel config flag
    config = "release" if build_type.lower() == "release" else "debug"

    output_dir = os.path.join(root_dir, "functionsystem", "output")
    bin_output_dir = os.path.join(output_dir, "bin")
    os.makedirs(bin_output_dir, exist_ok=True)

    log.info(f"Running Bazel build with config={config}, jobs={job_num}")

    # Place Bazel output under workspace/build/ (bind-mounted via -v /home/:/home/).
    # Without this, the default output root lands on Docker overlayfs (/root/.cache/bazel/),
    # which is NOT a bind mount, so linux-sandbox cannot resolve symlinks into the execroot.
    # This mirrors yuanrong's build.sh: --output_user_root="${BASE_DIR}/build".
    bazel_output_root = os.path.join(root_dir, "build", "bazel_root")
    os.makedirs(bazel_output_root, exist_ok=True)

    # thirdparty/runtime_deps holds pre-downloaded tarballs (e.g. rules_apple)
    # that are not available on the Huawei mirror.  Mirrors yuanrong's
    # --distdir=./thirdparty/runtime_deps pattern.
    distdir = os.path.join(root_dir, "thirdparty", "runtime_deps")

    # Build all binary targets
    bazel_cmd = [
        "bazel",
        f"--output_user_root={bazel_output_root}",
        "build",
        f"--distdir={distdir}",
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
