# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import shutil

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


def build_rust_binaries(root_dir):
    """Build all Rust workspace binaries and install to functionsystem/output/bin/."""
    root_dir = os.path.abspath(root_dir)
    output_bin = os.path.join(root_dir, "functionsystem", "output", "bin")
    os.makedirs(output_bin, exist_ok=True)

    log.info("Building Rust workspace with cargo build --workspace --release")
    utils.sync_command(
        cmd=["cargo", "build", "--workspace", "--release"],
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
