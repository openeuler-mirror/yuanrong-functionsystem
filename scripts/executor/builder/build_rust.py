# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import shutil
from glob import glob

import utils

log = utils.stream_logger()

# Rust binary crate names that produce executables
RUST_BINARIES = [
    "function_proxy",
    "function_master",
    "function_agent",
    "domain_scheduler",
    "runtime_manager",
    "iam_server",
    "meta_store",
]


def build_rust_binaries(root_dir, job_num):
    """Build all Rust workspace binaries and install to functionsystem/output/bin/."""
    root_dir = os.path.abspath(root_dir)
    output_bin = os.path.join(root_dir, "functionsystem", "output", "bin")
    output_lib = os.path.join(root_dir, "functionsystem", "output", "lib")
    os.makedirs(output_bin, exist_ok=True)
    os.makedirs(output_lib, exist_ok=True)
    os.makedirs(os.path.join(output_lib, "cmake"), exist_ok=True)
    os.makedirs(os.path.join(output_lib, "pkgconfig"), exist_ok=True)

    log.info(f"Building Rust workspace with cargo build --workspace --release -j {job_num}")
    utils.sync_command(
        cmd=["cargo", "build", "--workspace", "--release", "-j", str(job_num)],
        cwd=root_dir,
    )

    # Copy release binaries to output/bin/
    release_dir = os.path.join(root_dir, "target", "release")
    copied = []
    for name in RUST_BINARIES:
        src = os.path.join(release_dir, name)
        dst = os.path.join(output_bin, name)
        if not os.path.isfile(src):
            log.warning(f"Rust binary not found: {src}")
            continue
        shutil.copy2(src, dst)
        os.chmod(dst, 0o755)
        copied.append(name)

    log.info(f"Installed {len(copied)} Rust binaries to {output_bin}: {', '.join(copied)}")
    if len(copied) != len(RUST_BINARIES):
        missing = set(RUST_BINARIES) - set(copied)
        raise RuntimeError(f"Missing Rust binaries: {missing}")

    _stage_runtime_libraries(root_dir, output_lib)


def _stage_runtime_libraries(root_dir: str, output_lib: str):
    """Populate functionsystem/output/lib with the runtime-facing shared libraries
    that official pack/install flows expect to exist after a build.
    """
    search_dirs = [
        os.path.join(root_dir, "common", "litebus", "output", "lib"),
        os.path.join(root_dir, "common", "logs", "output", "lib"),
        os.path.join(root_dir, "common", "metrics", "output", "lib"),
        os.path.join(root_dir, "vendor", "src", "datasystem", "sdk", "cpp", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "obs", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "curl", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "grpc", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "spdlog", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "securec", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "openssl", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "openssl", "lib64"),
        os.path.join(root_dir, "vendor", "output", "Install", "yaml", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "jemalloc", "lib"),
        os.path.join(root_dir, "vendor", "output", "Install", "opentelemetry", "lib"),
    ]

    copied = set()
    for src_dir in search_dirs:
        if not os.path.isdir(src_dir):
            continue
        for so_path in glob(os.path.join(src_dir, "lib*.so*")):
            name = os.path.basename(so_path)
            dst = os.path.join(output_lib, name)
            if name in copied:
                continue
            shutil.copy2(so_path, dst)
            copied.add(name)

    log.info(f"Staged {len(copied)} runtime shared libraries to {output_lib}")
